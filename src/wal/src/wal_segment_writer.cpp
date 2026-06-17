#include "wal/wal_segment_writer.hpp"

#include "wal/wal_alignment.hpp"
#include "wal/wal_checksum.hpp"
#include "wal/wal_file.hpp"
#include "wal/wal_segment_scanner.hpp"

#include <array>
#include <fstream>
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
        const auto should_write_header = !WalFile::exists(file_path_) || WalFile::size(file_path_) == 0;

        if (!should_write_header) {
            open_error_ = prepare_existing_segment(first_sequence);
            if (open_error_ != WalError::None) {
                return;
            }
        }

        file_.open(file_path_, std::ios::binary | std::ios::app);
        if (should_write_header) {
            write_segment_header();
            if (!file_) {
                open_error_ = WalError::CannotWriteFile;
            }
        }
    }

    WalSegmentWriter::~WalSegmentWriter()
    {
        if (file_.is_open()) {
            file_.flush();
        }
    }

    WalAppendResult WalSegmentWriter::append(
        RecordType record_type,
        std::span<const std::byte> payload)
    {
        if (open_error_ != WalError::None) {
            return {.status = WalAppendStatus::Failed, .error = open_error_, .position = last_position_};
        }

        if (!file_) {
            return {.status = WalAppendStatus::Failed, .error = WalError::CannotWriteFile, .position = last_position_};
        }

        return write_record(record_type, payload);
    }

    WalCommitResult WalSegmentWriter::commit()
    {
        if (open_error_ != WalError::None) {
            return {.status = WalCommitStatus::Failed, .error = open_error_, .committed_up_to = last_position_};
        }

        file_.flush();

        if (!file_) {
            return {.status = WalCommitStatus::Failed, .error = WalError::CannotFlushFile, .committed_up_to = last_position_};
        }

        for (const auto& position : pending_positions_) {
            committed_positions_.push(position);
            last_committed_position_ = position;
        }
        pending_positions_.clear();

        return {.status = WalCommitStatus::Committed, .error = WalError::None, .committed_up_to = last_committed_position_};
    }

    WalPosition WalSegmentWriter::last_position() const noexcept
    {
        return last_position_;
    }

    WalPosition WalSegmentWriter::last_committed_position() const noexcept
    {
        return last_committed_position_;
    }

    bool WalSegmentWriter::has_committed_position() const noexcept
    {
        return !committed_positions_.empty();
    }

    std::size_t WalSegmentWriter::pending_count() const noexcept
    {
        return pending_positions_.size();
    }

    std::size_t WalSegmentWriter::committed_queue_size() const noexcept
    {
        return committed_positions_.size();
    }

    bool WalSegmentWriter::pop_committed_position(WalPosition& out)
    {
        if (committed_positions_.empty()) {
            return false;
        }

        out = committed_positions_.front();
        committed_positions_.pop();
        return true;
    }

    WalAppendResult WalSegmentWriter::write_record(
        RecordType record_type,
        std::span<const std::byte> payload)
    {
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
            return {.status = WalAppendStatus::Failed, .error = WalError::CannotWriteFile, .position = last_position_};
        }

        last_position_ = {stream_id_, epoch_, next_sequence_++};
        pending_positions_.push_back(last_position_);
        return {.status = WalAppendStatus::Appended, .error = WalError::None, .position = last_position_};
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
        last_committed_position_ = last_position_;
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
}
