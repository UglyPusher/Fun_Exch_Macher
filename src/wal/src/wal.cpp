/**
 * @file wal.cpp
 * @brief Implements the public durable WAL message-queue facade.
 *
 * This file owns the boundary between normal application code and the current
 * binary segment implementation. Public append calls write, flush, fsync, and
 * only then advance committed visibility. Keep segment headers, CRC, padding,
 * and recovery details behind this facade.
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
#include <fstream>
#include <limits>
#include <unordered_map>
#include <utility>

namespace wal
{
    namespace
    {
        [[nodiscard]] bool can_add_size(std::size_t left, std::size_t right) noexcept
        {
            return left <= std::numeric_limits<std::size_t>::max() - right;
        }

        [[nodiscard]] bool can_encode_physical_record(std::span<const std::byte> payload) noexcept
        {
            constexpr std::size_t max_uint32 = std::numeric_limits<std::uint32_t>::max();
            constexpr std::size_t header_size = sizeof(WalRecordHeader);
            constexpr std::size_t alignment_slack = DefaultRecordAlignment - 1U;

            if (!can_add_size(header_size, payload.size())) {
                return false;
            }

            const std::size_t raw_record_size = header_size + payload.size();
            if (raw_record_size > max_uint32) {
                return false;
            }

            if (!can_add_size(raw_record_size, alignment_slack)) {
                return false;
            }

            return raw_record_size + alignment_slack <= max_uint32;
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

                WalRecord committed_record = build_physical_record(record_view, read_result);
                if (committed_record.position.sequence != next_message_sequence_) {
                    wal_corrupted_ = true;
                    wal_error_ = WalError::UnexpectedSequence;
                    return;
                }

                message_states_[committed_record.position.sequence] = WalMessageState::Committed;
                committed_records_.push_back(std::move(committed_record));
                ++next_message_sequence_;
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
                return corrupted_batch_append_result(WalError::CorruptedMiddleRecord, messages.size());
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
                    .messages_appended = 0,
                    .messages_committed = 0
                };
            }

            if (!validate_message_payloads_fit_physical_records(messages)) {
                return rejected_batch_append_result(WalError::InvalidPayloadLength, messages.size());
            }

            const WalSeq first_batch_sequence = next_message_sequence_;
            const std::uint64_t committed_file_size_before_append = WalFile::size(config_.path);
            std::vector<WalRecord> staged_records;
            staged_records.reserve(messages.size());

            for (const WalMessageView& message : messages) {
                const WalRawAppendResult append_result = writer_->append(message.record_type, message.payload);
                if (append_result.status != WalRawAppendStatus::Appended) {
                    truncate_failed_append(committed_file_size_before_append);
                    wal_failed_ = true;
                    wal_error_ = append_result.error;
                    if (append_result.status == WalRawAppendStatus::Rejected) {
                        return rejected_batch_append_result(append_result.error, messages.size());
                    }
                    return failed_batch_append_result(append_result.error, messages.size());
                }

                WalRecord staged_record;
                staged_record.record_type = message.record_type;
                staged_record.position = append_result.position;
                staged_record.payload.assign(message.payload.begin(), message.payload.end());
                staged_records.push_back(std::move(staged_record));
            }

            const WalFlushResult flush_result = writer_->flush_pending_writes();
            if (flush_result.status != WalFlushStatus::Flushed) {
                truncate_failed_append(committed_file_size_before_append);
                wal_failed_ = true;
                wal_error_ = flush_result.error;
                return failed_batch_append_result(flush_result.error, messages.size());
            }

            if (!WalFile::sync(config_.path)) {
                truncate_failed_append(committed_file_size_before_append);
                wal_failed_ = true;
                wal_error_ = WalError::CannotSyncFile;
                return failed_batch_append_result(WalError::CannotSyncFile, messages.size());
            }

            publish_committed_batch(std::move(staged_records));

            return {
                .status = WalAppendStatus::Committed,
                .error = WalError::None,
                .first_sequence = first_batch_sequence,
                .last_sequence = first_batch_sequence + messages.size() - 1,
                .messages_appended = messages.size(),
                .messages_committed = messages.size()
            };
        }

        [[nodiscard]] bool validate_message_payloads_fit_physical_records(
            std::span<const WalMessageView> messages) const noexcept
        {
            return std::ranges::all_of(messages, [](const WalMessageView& message) {
                return can_encode_physical_record(message.payload);
            });
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

        [[nodiscard]] WalBatchAppendResult failed_batch_append_result(
            WalError error,
            std::size_t message_count) const noexcept
        {
            return {
                .status = WalAppendStatus::Failed,
                .error = error,
                .first_sequence = next_message_sequence_,
                .last_sequence = message_count == 0 ? 0 : next_message_sequence_ + message_count - 1,
                .messages_appended = 0,
                .messages_committed = 0
            };
        }

        [[nodiscard]] WalBatchAppendResult corrupted_batch_append_result(
            WalError error,
            std::size_t message_count) const noexcept
        {
            return {
                .status = WalAppendStatus::Corrupted,
                .error = error,
                .first_sequence = next_message_sequence_,
                .last_sequence = message_count == 0 ? 0 : next_message_sequence_ + message_count - 1,
                .messages_appended = 0,
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
                .messages_appended = 0,
                .messages_committed = 0
            };
        }

        void publish_committed_batch(std::vector<WalRecord> staged_records)
        {
            for (WalRecord& committed_record : staged_records) {
                message_states_[committed_record.position.sequence] = WalMessageState::Committed;
                committed_records_.push_back(std::move(committed_record));
                ++next_message_sequence_;
            }
        }

        [[nodiscard]] WalReadResult read_next_message(WalCursor& cursor, WalRecord& out)
        {
            if (wal_corrupted_) {
                return {
                    .status = WalReadStatus::Corrupted,
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
                    .status = WalReadStatus::InvalidArgument,
                    .error = WalError::InvalidPayloadLength,
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
                        .status = read_result.status,
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
