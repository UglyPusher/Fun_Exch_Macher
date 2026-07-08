/**
 * @file wal_segment_writer.cpp
 * @brief Implements append, reopen, and flush handling for one WAL segment.
 */

#include "wal/wal_segment_writer.hpp"

#include "wal/wal_alignment.hpp"
#include "wal/wal_checksum.hpp"
#include "wal/wal_file.hpp"
#include "wal/wal_segment_scanner.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <utility>

namespace wal
{
    WalSegmentWriter::WalSegmentWriter(
        std::filesystem::path file_path,
        StreamId stream_id,
        EpochId epoch,
        SequenceNumber first_sequence)
        : file_path_(std::move(file_path)),
          stream_id_(stream_id),
          epoch_(epoch),
          next_sequence_(first_sequence)
    {
        WalFile::ensure_parent_directory_exists(file_path_);
        if (std::filesystem::is_directory(file_path_)) {
            fail_writer(WalError::CannotWriteFile);
            return;
        }

        const auto should_write_header = !WalFile::exists(file_path_) || WalFile::size(file_path_) == 0;

        if (!should_write_header) {
            writer_error_ = prepare_existing_segment(first_sequence);
            if (writer_error_ != WalError::None) {
                return;
            }
        }

        file_.open(file_path_, std::ios::binary | std::ios::app);
        if (should_write_header) {
            write_segment_header();
            if (!file_) {
                fail_writer(WalError::CannotWriteFile);
            }
        }
    }

    WalSegmentWriter::~WalSegmentWriter()
    {
        if (file_.is_open()) {
            file_.flush();
        }
    }

    WalRawAppendResult WalSegmentWriter::append(
        RecordType record_type,
        std::span<const std::byte> payload)
    {
        if (writer_error_ != WalError::None) {
            return {.status = WalRawAppendStatus::Failed, .error = writer_error_, .position = last_position_};
        }

        if (!file_) {
            fail_writer(WalError::CannotWriteFile);
            return {.status = WalRawAppendStatus::Failed, .error = WalError::CannotWriteFile, .position = last_position_};
        }

        return write_record(record_type, payload);
    }

    WalFlushResult WalSegmentWriter::flush_pending_writes()
    {
        if (writer_error_ != WalError::None) {
            return {.status = WalFlushStatus::Failed, .error = writer_error_, .flushed_up_to = last_position_};
        }

        file_.flush();

        if (!file_) {
            fail_writer(WalError::CannotFlushFile);
            return {.status = WalFlushStatus::Failed, .error = WalError::CannotFlushFile, .flushed_up_to = last_position_};
        }

        const WalPosition flushed_up_to = pending_positions_.empty()
            ? last_position_
            : pending_positions_.back();
        pending_positions_.clear();

        return {.status = WalFlushStatus::Flushed, .error = WalError::None, .flushed_up_to = flushed_up_to};
    }

    WalPosition WalSegmentWriter::last_position() const noexcept
    {
        return last_position_;
    }

    std::size_t WalSegmentWriter::pending_count() const noexcept
    {
        return pending_positions_.size();
    }

    WalRawAppendResult WalSegmentWriter::write_record(
        RecordType record_type,
        std::span<const std::byte> payload)
    {
        constexpr auto max_record_length = std::numeric_limits<std::uint32_t>::max();
        if (payload.size() > max_record_length - sizeof(WalRecordHeader)) {
            return {.status = WalRawAppendStatus::Rejected, .error = WalError::InvalidPayloadLength, .position = last_position_};
        }

        const auto raw_record_length = sizeof(WalRecordHeader) + static_cast<std::uint32_t>(payload.size());
        if (raw_record_length > max_record_length - (DefaultRecordAlignment - 1)) {
            return {.status = WalRawAppendStatus::Rejected, .error = WalError::InvalidRecordLength, .position = last_position_};
        }

        auto header = make_header(record_type, payload, next_sequence_);

        file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!payload.empty()) {
            file_.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        }

        const auto padding = padding_for(sizeof(WalRecordHeader) + static_cast<std::uint32_t>(payload.size()), DefaultRecordAlignment);
        if (padding != 0) {
            const std::array<std::byte, DefaultRecordAlignment> zeros{};
            file_.write(reinterpret_cast<const char*>(zeros.data()), padding);
        }

        if (!file_) {
            fail_writer(WalError::CannotWriteFile);
            return {.status = WalRawAppendStatus::Failed, .error = WalError::CannotWriteFile, .position = last_position_};
        }

        last_position_ = {stream_id_, epoch_, next_sequence_++};
        pending_positions_.push_back(last_position_);
        return {.status = WalRawAppendStatus::Appended, .error = WalError::None, .position = last_position_};
    }

    WalRecordHeader WalSegmentWriter::make_header(
        RecordType record_type,
        std::span<const std::byte> payload,
        SequenceNumber sequence) const
    {
        const auto raw_length = sizeof(WalRecordHeader) + static_cast<std::uint32_t>(payload.size());
        WalRecordHeader header{
            .record_length = align_up(raw_length, DefaultRecordAlignment),
            .record_type = record_type,
            .stream_id = stream_id_,
            .epoch = epoch_,
            .sequence = sequence,
            .payload_length = static_cast<std::uint32_t>(payload.size()),
            .payload_crc = calculate_crc32(payload)
        };
        header.header_crc = calculate_record_header_crc(header);
        return header;
    }

    WalError WalSegmentWriter::prepare_existing_segment(SequenceNumber first_sequence)
    {
        WalSegmentHeader segment_header;
        const auto header_error = read_existing_segment_header(segment_header);
        if (header_error != WalError::None) {
            return header_error;
        }

        if (segment_header.stream_id != stream_id_
            || segment_header.epoch != epoch_
            || segment_header.first_sequence != first_sequence) {
            return WalError::InvalidSegmentHeader;
        }

        WalSegmentScanner scanner;
        auto scan_result = scanner.scan(file_path_);
        if (!scan_result.ok && scan_result.error != WalError::IncompleteTrailingRecord) {
            return scan_result.error;
        }

        if (scan_result.error == WalError::IncompleteTrailingRecord) {
            WalFile::truncate(file_path_, scan_result.last_valid_offset);
        }

        last_position_ = scan_result.last_valid_position;
        next_sequence_ = last_position_.is_valid()
            ? last_position_.sequence + 1
            : segment_header.first_sequence;

        return WalError::None;
    }

    WalError WalSegmentWriter::read_existing_segment_header(WalSegmentHeader& header) const
    {
        std::ifstream file{file_path_, std::ios::binary};
        if (!file) {
            return WalError::CannotOpenFile;
        }

        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(header))) {
            return WalError::InvalidSegmentHeader;
        }

        if (!header.has_valid_static_fields()) {
            return WalError::InvalidSegmentHeader;
        }

        if (header.header_crc != calculate_segment_header_crc(header)) {
            return WalError::HeaderChecksumMismatch;
        }

        return WalError::None;
    }

    void WalSegmentWriter::write_segment_header()
    {
        WalSegmentHeader header{
            .stream_id = stream_id_,
            .epoch = epoch_,
            .first_sequence = next_sequence_
        };
        header.header_crc = calculate_segment_header_crc(header);
        file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }

    void WalSegmentWriter::fail_writer(WalError error) noexcept
    {
        if (writer_error_ == WalError::None) {
            writer_error_ = error;
        }
    }
}
