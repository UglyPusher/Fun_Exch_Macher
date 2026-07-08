/**
 * @file wal.cpp
 * @brief Implements the public durable WAL message-queue facade.
 *
 * This file owns the boundary between normal application code and the current
 * binary segment implementation. Public append calls write, flush, fsync, and
 * only then advance committed visibility. Keep segment headers, CRC, padding,
 * batch envelopes, and recovery details behind this facade.
 */

#include "wal/wal.hpp"

#include "wal/wal_checksum.hpp"
#include "wal/wal_file.hpp"
#include "wal/wal_record_view.hpp"
#include "wal/wal_segment_header.hpp"
#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_scanner.hpp"
#include "wal/wal_segment_writer.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace wal
{
    namespace
    {
        constexpr RecordType internal_batch_record_type = 0x57424C42; // "WBLB"
        constexpr std::uint32_t batch_payload_magic = 0x57424C50;     // "WBLP"
        constexpr std::uint16_t batch_payload_version = 1;

        struct BatchPayloadHeader
        {
            std::uint32_t magic = batch_payload_magic;
            std::uint16_t version = batch_payload_version;
            std::uint16_t reserved = 0;
            WalSeq first_sequence = 0;
            std::uint32_t message_count = 0;
        };

        struct BatchMessageHeader
        {
            RecordType record_type = 0;
            std::uint32_t payload_size = 0;
        };

        static_assert(std::is_trivially_copyable_v<BatchPayloadHeader>);
        static_assert(std::is_trivially_copyable_v<BatchMessageHeader>);

        [[nodiscard]] bool fits_uint32(std::size_t value) noexcept
        {
            return value <= std::numeric_limits<std::uint32_t>::max();
        }

        [[nodiscard]] bool can_add_size(std::size_t left, std::size_t right) noexcept
        {
            return left <= std::numeric_limits<std::size_t>::max() - right;
        }

        template <typename TValue>
        void append_scalar_bytes(std::vector<std::byte>& payload, const TValue& value)
        {
            const auto* first_byte = reinterpret_cast<const std::byte*>(&value);
            payload.insert(payload.end(), first_byte, first_byte + sizeof(TValue));
        }

        template <typename TValue>
        [[nodiscard]] bool read_scalar_bytes(
            std::span<const std::byte> payload,
            std::size_t& byte_offset,
            TValue& value) noexcept
        {
            if (byte_offset > payload.size() || payload.size() - byte_offset < sizeof(TValue)) {
                return false;
            }

            std::memcpy(&value, payload.data() + byte_offset, sizeof(TValue));
            byte_offset += sizeof(TValue);
            return true;
        }

        [[nodiscard]] bool validate_batch_payload_size(std::span<const WalMessageView> messages) noexcept
        {
            std::size_t encoded_payload_size = sizeof(BatchPayloadHeader);
            for (const WalMessageView& message : messages) {
                if (!fits_uint32(message.payload.size())) {
                    return false;
                }

                if (!can_add_size(encoded_payload_size, sizeof(BatchMessageHeader))) {
                    return false;
                }
                encoded_payload_size += sizeof(BatchMessageHeader);

                if (!can_add_size(encoded_payload_size, message.payload.size())) {
                    return false;
                }
                encoded_payload_size += message.payload.size();
            }

            return fits_uint32(encoded_payload_size);
        }

        [[nodiscard]] std::vector<std::byte> encode_batch_payload(
            WalSeq first_sequence,
            std::span<const WalMessageView> messages)
        {
            std::vector<std::byte> encoded_payload;
            BatchPayloadHeader batch_header{
                .first_sequence = first_sequence,
                .message_count = static_cast<std::uint32_t>(messages.size())
            };
            append_scalar_bytes(encoded_payload, batch_header);

            for (const WalMessageView& message : messages) {
                BatchMessageHeader message_header{
                    .record_type = message.record_type,
                    .payload_size = static_cast<std::uint32_t>(message.payload.size())
                };
                append_scalar_bytes(encoded_payload, message_header);
                encoded_payload.insert(encoded_payload.end(), message.payload.begin(), message.payload.end());
            }

            return encoded_payload;
        }

        [[nodiscard]] bool decode_batch_payload(
            const WalRecord& physical_record,
            std::vector<WalRecord>& decoded_messages)
        {
            std::size_t byte_offset = 0;
            BatchPayloadHeader batch_header;
            if (!read_scalar_bytes(std::span<const std::byte>{physical_record.payload}, byte_offset, batch_header)) {
                return false;
            }

            if (batch_header.magic != batch_payload_magic || batch_header.version != batch_payload_version) {
                return false;
            }

            decoded_messages.clear();
            decoded_messages.reserve(batch_header.message_count);

            for (std::uint32_t message_index = 0; message_index < batch_header.message_count; ++message_index) {
                BatchMessageHeader message_header;
                if (!read_scalar_bytes(std::span<const std::byte>{physical_record.payload}, byte_offset, message_header)) {
                    return false;
                }

                if (physical_record.payload.size() - byte_offset < message_header.payload_size) {
                    return false;
                }

                WalRecord decoded_message;
                decoded_message.record_type = message_header.record_type;
                decoded_message.position = {
                    physical_record.position.stream_id,
                    physical_record.position.epoch,
                    batch_header.first_sequence + message_index
                };
                decoded_message.payload.assign(
                    physical_record.payload.begin() + static_cast<std::ptrdiff_t>(byte_offset),
                    physical_record.payload.begin() + static_cast<std::ptrdiff_t>(byte_offset + message_header.payload_size));
                byte_offset += message_header.payload_size;
                decoded_messages.push_back(std::move(decoded_message));
            }

            return byte_offset == physical_record.payload.size();
        }

        [[nodiscard]] WalRecord build_physical_record(const WalRecordView& record_view, const WalReadResult& read_result)
        {
            WalRecord physical_record;
            physical_record.record_type = record_view.header.record_type;
            physical_record.position = read_result.position;
            physical_record.payload.assign(record_view.payload.begin(), record_view.payload.end());
            return physical_record;
        }
    }

    bool WalAppendResult::ok() const noexcept
    {
        return status == WalAppendStatus::Committed;
    }

    bool WalBatchAppendResult::ok() const noexcept
    {
        return status == WalAppendStatus::Committed;
    }

    bool WalBatchReadResult::ok() const noexcept
    {
        return status == WalReadStatus::RecordRead || status == WalReadStatus::EndOfLog;
    }

    struct Wal::Impl
    {
        explicit Impl(WalConfig config)
            : config_(std::move(config))
        {
            open_or_recover_existing_wal();
        }

        WalConfig config_;
        std::unique_ptr<WalSegmentWriter> writer_;
        std::vector<WalRecord> committed_records_;
        std::unordered_map<WalSeq, WalMessageState> message_states_;

        WalSeq next_message_sequence_ = 1;
        bool wal_failed_ = false;
        bool wal_corrupted_ = false;
        WalError wal_error_ = WalError::None;

        void open_or_recover_existing_wal()
        {
            if (WalFile::exists(config_.path) && WalFile::size(config_.path) != 0) {
                adopt_existing_segment_header();
                if (wal_corrupted_) {
                    return;
                }

                next_message_sequence_ = config_.first_sequence;
                recover_existing_file_before_opening_writer();
                if (wal_corrupted_) {
                    return;
                }
                load_committed_records_from_file();
                if (wal_corrupted_) {
                    return;
                }
            } else {
                next_message_sequence_ = config_.first_sequence;
            }

            writer_ = std::make_unique<WalSegmentWriter>(
                config_.path,
                config_.stream_id,
                config_.epoch,
                config_.first_sequence);
        }

        void adopt_existing_segment_header()
        {
            WalSegmentHeader segment_header;
            std::ifstream file{config_.path, std::ios::binary};
            file.read(reinterpret_cast<char*>(&segment_header), sizeof(segment_header));
            if (!file || file.gcount() != static_cast<std::streamsize>(sizeof(segment_header))) {
                wal_corrupted_ = true;
                wal_error_ = WalError::InvalidSegmentHeader;
                return;
            }

            if (!segment_header.has_valid_static_fields()
                || segment_header.header_crc != calculate_segment_header_crc(segment_header)) {
                wal_corrupted_ = true;
                wal_error_ = WalError::InvalidSegmentHeader;
                return;
            }

            config_.stream_id = segment_header.stream_id;
            config_.epoch = segment_header.epoch;
            config_.first_sequence = segment_header.first_sequence;
        }

        void recover_existing_file_before_opening_writer()
        {
            WalSegmentScanner scanner;
            const WalSegmentScanResult scan_result = scanner.scan(config_.path);

            if (scan_result.ok) {
                return;
            }

            if (scan_result.error == WalError::IncompleteTrailingRecord) {
                WalFile::truncate(config_.path, scan_result.last_valid_offset);
                return;
            }

            wal_corrupted_ = true;
            wal_error_ = scan_result.error;
        }

        void load_committed_records_from_file()
        {
            WalSegmentReader reader{config_.path};

            while (true) {
                WalRecordView record_view;
                const WalReadResult read_result = reader.read_next(record_view);
                if (read_result.status == WalReadStatus::EndOfLog) {
                    return;
                }

                if (read_result.status != WalReadStatus::RecordRead) {
                    wal_corrupted_ = true;
                    wal_error_ = read_result.error;
                    return;
                }

                const WalRecord physical_record = build_physical_record(record_view, read_result);
                if (physical_record.record_type != internal_batch_record_type) {
                    wal_corrupted_ = true;
                    wal_error_ = WalError::UnknownRecordType;
                    return;
                }

                std::vector<WalRecord> decoded_messages;
                if (!decode_batch_payload(physical_record, decoded_messages)) {
                    wal_corrupted_ = true;
                    wal_error_ = WalError::InvalidPayloadLength;
                    return;
                }

                for (WalRecord& decoded_message : decoded_messages) {
                    if (decoded_message.position.sequence != next_message_sequence_) {
                        wal_corrupted_ = true;
                        wal_error_ = WalError::UnexpectedSequence;
                        return;
                    }

                    message_states_[decoded_message.position.sequence] = WalMessageState::Committed;
                    committed_records_.push_back(std::move(decoded_message));
                    ++next_message_sequence_;
                }
            }
        }

        [[nodiscard]] WalSeq committed_frontier() const noexcept
        {
            return next_message_sequence_ == config_.first_sequence
                ? config_.first_sequence - 1
                : next_message_sequence_ - 1;
        }

        [[nodiscard]] WalAppendResult append_one_message(WalMessageView message)
        {
            const WalBatchAppendResult batch_result = append_message_batch(std::span<const WalMessageView>{&message, 1});
            return {
                .status = batch_result.status,
                .error = batch_result.error,
                .position = {
                    config_.stream_id,
                    config_.epoch,
                    batch_result.first_sequence
                }
            };
        }

        [[nodiscard]] WalBatchAppendResult append_message_batch(std::span<const WalMessageView> messages)
        {
            if (wal_corrupted_) {
                return failed_batch_append_result(WalError::CorruptedMiddleRecord, messages.size());
            }

            if (wal_failed_) {
                return failed_batch_append_result(wal_error_, messages.size());
            }

            if (messages.empty()) {
                return {
                    .status = WalAppendStatus::Committed,
                    .error = WalError::None,
                    .first_sequence = next_message_sequence_,
                    .last_sequence = next_message_sequence_ == config_.first_sequence ? 0 : next_message_sequence_ - 1,
                    .messages_committed = 0
                };
            }

            if (!validate_batch_payload_size(messages)) {
                mark_batch_failed(messages.size());
                return rejected_batch_append_result(WalError::InvalidPayloadLength, messages.size());
            }

            const WalSeq first_batch_sequence = next_message_sequence_;
            const std::uint64_t committed_file_size_before_append = WalFile::size(config_.path);
            const std::vector<std::byte> encoded_payload = encode_batch_payload(first_batch_sequence, messages);

            const WalRawAppendResult append_result = writer_->append(internal_batch_record_type, encoded_payload);
            if (append_result.status != WalRawAppendStatus::Appended) {
                mark_batch_failed(messages.size());
                wal_failed_ = true;
                wal_error_ = append_result.error;
                return failed_batch_append_result(append_result.error, messages.size());
            }

            const WalCommitResult commit_result = writer_->commit();
            if (commit_result.status != WalCommitStatus::Committed) {
                truncate_failed_append(committed_file_size_before_append);
                mark_batch_failed(messages.size());
                wal_failed_ = true;
                wal_error_ = commit_result.error;
                return failed_batch_append_result(commit_result.error, messages.size());
            }

            if (!WalFile::sync(config_.path)) {
                truncate_failed_append(committed_file_size_before_append);
                mark_batch_failed(messages.size());
                wal_failed_ = true;
                wal_error_ = WalError::CannotSyncFile;
                return failed_batch_append_result(WalError::CannotSyncFile, messages.size());
            }

            publish_committed_batch(messages, first_batch_sequence);

            return {
                .status = WalAppendStatus::Committed,
                .error = WalError::None,
                .first_sequence = first_batch_sequence,
                .last_sequence = first_batch_sequence + messages.size() - 1,
                .messages_committed = messages.size()
            };
        }

        void truncate_failed_append(std::uint64_t committed_file_size_before_append)
        {
            try {
                WalFile::truncate(config_.path, committed_file_size_before_append);
            } catch (...) {
                // The caller already receives a failed append result. A failed
                // rollback leaves the WAL in failed state for this process.
            }
        }

        void mark_batch_failed(std::size_t message_count)
        {
            for (std::size_t message_index = 0; message_index < message_count; ++message_index) {
                message_states_[next_message_sequence_ + message_index] = WalMessageState::Failed;
            }
        }

        [[nodiscard]] WalBatchAppendResult failed_batch_append_result(
            WalError error,
            std::size_t message_count) const noexcept
        {
            return {
                .status = WalAppendStatus::Failed,
                .error = error,
                .first_sequence = next_message_sequence_,
                .last_sequence = message_count == 0 ? 0 : next_message_sequence_ + message_count - 1,
                .messages_committed = 0
            };
        }

        [[nodiscard]] WalBatchAppendResult rejected_batch_append_result(
            WalError error,
            std::size_t message_count) const noexcept
        {
            return {
                .status = WalAppendStatus::Rejected,
                .error = error,
                .first_sequence = next_message_sequence_,
                .last_sequence = message_count == 0 ? 0 : next_message_sequence_ + message_count - 1,
                .messages_committed = 0
            };
        }

        void publish_committed_batch(std::span<const WalMessageView> messages, WalSeq first_batch_sequence)
        {
            for (std::size_t message_index = 0; message_index < messages.size(); ++message_index) {
                const WalMessageView message = messages[message_index];
                WalRecord committed_record;
                committed_record.record_type = message.record_type;
                committed_record.position = {
                    config_.stream_id,
                    config_.epoch,
                    first_batch_sequence + message_index
                };
                committed_record.payload.assign(message.payload.begin(), message.payload.end());

                message_states_[committed_record.position.sequence] = WalMessageState::Committed;
                committed_records_.push_back(std::move(committed_record));
                ++next_message_sequence_;
            }
        }

        [[nodiscard]] WalReadResult read_next_message(WalCursor& cursor, WalRecord& out)
        {
            if (wal_corrupted_) {
                return {
                    .status = WalReadStatus::Failed,
                    .error = WalError::CorruptedMiddleRecord,
                    .position = {config_.stream_id, config_.epoch, cursor.next_sequence}
                };
            }

            if (cursor.next_sequence < config_.first_sequence) {
                return {
                    .status = WalReadStatus::Failed,
                    .error = WalError::UnexpectedSequence,
                    .position = {config_.stream_id, config_.epoch, cursor.next_sequence}
                };
            }

            if (cursor.next_sequence > committed_frontier()) {
                return {
                    .status = WalReadStatus::EndOfLog,
                    .error = WalError::EndOfLog,
                    .position = {config_.stream_id, config_.epoch, committed_frontier()}
                };
            }

            const std::size_t record_index = static_cast<std::size_t>(cursor.next_sequence - config_.first_sequence);
            out = committed_records_[record_index];
            ++cursor.next_sequence;

            return {
                .status = WalReadStatus::RecordRead,
                .error = WalError::None,
                .position = out.position
            };
        }

        [[nodiscard]] WalBatchReadResult read_message_batch(WalCursor& cursor, std::span<WalRecord> out)
        {
            if (out.empty()) {
                return {
                    .status = WalReadStatus::EndOfLog,
                    .error = WalError::EndOfLog,
                    .records_read = 0,
                    .last_position = {config_.stream_id, config_.epoch, cursor.next_sequence}
                };
            }

            std::size_t records_read = 0;
            WalPosition last_position{};
            while (records_read < out.size()) {
                WalReadResult read_result = read_next_message(cursor, out[records_read]);
                if (read_result.status == WalReadStatus::EndOfLog) {
                    return {
                        .status = records_read == 0 ? WalReadStatus::EndOfLog : WalReadStatus::RecordRead,
                        .error = records_read == 0 ? WalError::EndOfLog : WalError::None,
                        .records_read = records_read,
                        .last_position = last_position
                    };
                }

                if (read_result.status != WalReadStatus::RecordRead) {
                    return {
                        .status = WalReadStatus::Failed,
                        .error = read_result.error,
                        .records_read = records_read,
                        .last_position = last_position
                    };
                }

                last_position = read_result.position;
                ++records_read;
            }

            return {
                .status = WalReadStatus::RecordRead,
                .error = WalError::None,
                .records_read = records_read,
                .last_position = last_position
            };
        }
    };

    Wal::Wal(WalConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    Wal::~Wal() = default;

    Wal::Wal(Wal&&) noexcept = default;

    Wal& Wal::operator=(Wal&&) noexcept = default;

    WalAppendResult Wal::append(WalMessageView message)
    {
        return impl_->append_one_message(message);
    }

    WalBatchAppendResult Wal::append_batch(std::span<const WalMessageView> messages)
    {
        return impl_->append_message_batch(messages);
    }

    WalReadResult Wal::read_next(WalCursor& cursor, WalRecord& out)
    {
        return impl_->read_next_message(cursor, out);
    }

    WalBatchReadResult Wal::read_batch(WalCursor& cursor, std::span<WalRecord> out)
    {
        return impl_->read_message_batch(cursor, out);
    }

    WalMessageState Wal::get_message_state(WalSeq sequence) const noexcept
    {
        const auto message_state_position = impl_->message_states_.find(sequence);
        if (message_state_position != impl_->message_states_.end()) {
            return message_state_position->second;
        }

        if (impl_->wal_corrupted_ && sequence > impl_->committed_frontier()) {
            return WalMessageState::Corrupted;
        }

        return WalMessageState::Unknown;
    }

    WalCursor Wal::cursor_from_beginning() const noexcept
    {
        return WalCursor{.next_sequence = impl_->config_.first_sequence};
    }

    WalCursor Wal::cursor_from(WalSeq sequence) const noexcept
    {
        return WalCursor{.next_sequence = sequence};
    }

    WalCursor Wal::cursor_from_end() const noexcept
    {
        return WalCursor{.next_sequence = impl_->next_message_sequence_};
    }

    WalRecoveryResult scan_wal_segment(const std::filesystem::path& file_path)
    {
        WalSegmentScanner scanner;
        const WalSegmentScanResult scan_result = scanner.scan(file_path);
        return {
            .ok = scan_result.ok,
            .error = scan_result.error,
            .last_valid_position = scan_result.last_valid_position,
            .last_valid_offset = scan_result.last_valid_offset,
            .recovered_incomplete_tail = false
        };
    }

    WalRecoveryResult recover_wal_segment(const std::filesystem::path& file_path)
    {
        WalSegmentScanner scanner;
        const WalSegmentScanResult scan_result = scanner.scan(file_path);
        if (scan_result.ok) {
            return {
                .ok = true,
                .error = WalError::None,
                .last_valid_position = scan_result.last_valid_position,
                .last_valid_offset = scan_result.last_valid_offset,
                .recovered_incomplete_tail = false
            };
        }

        if (scan_result.error == WalError::IncompleteTrailingRecord) {
            WalFile::truncate(file_path, scan_result.last_valid_offset);
            return {
                .ok = true,
                .error = WalError::None,
                .last_valid_position = scan_result.last_valid_position,
                .last_valid_offset = scan_result.last_valid_offset,
                .recovered_incomplete_tail = true
            };
        }

        return {
            .ok = false,
            .error = scan_result.error,
            .last_valid_position = scan_result.last_valid_position,
            .last_valid_offset = scan_result.last_valid_offset,
            .recovered_incomplete_tail = false
        };
    }
}
