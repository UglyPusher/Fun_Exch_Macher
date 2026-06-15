#include "wal/wal_segment_reader.hpp"

#include "wal/wal_alignment.hpp"
#include "wal/wal_checksum.hpp"

#include <utility>

namespace wal
{
    WalSegmentReader::WalSegmentReader(std::filesystem::path file_path)
        : file_path_(std::move(file_path))
    {
        file_.open(file_path_, std::ios::binary);
        open_result_ = file_
            ? read_segment_header()
            : WalReadResult{.status = WalReadStatus::Failed, .error = WalError::CannotOpenFile, .position = last_position_};
    }

    WalReadResult WalSegmentReader::read_next(WalRecordView& out)
    {
        if (open_result_.status != WalReadStatus::RecordRead) {
            return open_result_;
        }

        if (!file_) {
            return {.status = WalReadStatus::Failed, .error = WalError::CannotReadFile, .position = last_position_};
        }

        WalRecordHeader header;
        auto header_result = read_record_header(header);
        if (header_result.status != WalReadStatus::RecordRead) {
            return header_result;
        }

        auto payload_result = read_payload(header);
        if (payload_result.status != WalReadStatus::RecordRead) {
            return payload_result;
        }

        const auto padding = header.record_length - sizeof(WalRecordHeader) - header.payload_length;
        if (padding != 0) {
            file_.ignore(static_cast<std::streamsize>(padding));
            if (!file_) {
                return {.status = WalReadStatus::Failed, .error = WalError::CannotReadFile, .position = last_position_};
            }
        }

        last_position_ = {header.stream_id, header.epoch, header.sequence};
        out = {.header = header, .payload = std::span<const std::byte>{payload_buffer_.data(), payload_buffer_.size()}};
        return {.status = WalReadStatus::RecordRead, .error = WalError::None, .position = last_position_};
    }

    WalPosition WalSegmentReader::last_position() const noexcept
    {
        return last_position_;
    }

    WalReadResult WalSegmentReader::read_segment_header()
    {
        file_.read(reinterpret_cast<char*>(&segment_header_), sizeof(segment_header_));
        if (!file_) {
            return {.status = WalReadStatus::Failed, .error = WalError::CannotReadFile, .position = last_position_};
        }

        if (!segment_header_.has_valid_static_fields()) {
            return {.status = WalReadStatus::Failed, .error = WalError::InvalidSegmentHeader, .position = last_position_};
        }

        if (segment_header_.header_crc != calculate_segment_header_crc(segment_header_)) {
            return {.status = WalReadStatus::Failed, .error = WalError::HeaderChecksumMismatch, .position = last_position_};
        }

        return {.status = WalReadStatus::RecordRead, .error = WalError::None, .position = last_position_};
    }

    WalReadResult WalSegmentReader::read_record_header(WalRecordHeader& header)
    {
        file_.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (file_.gcount() == 0 && file_.eof()) {
            file_.clear();
            return {.status = WalReadStatus::EndOfLog, .error = WalError::EndOfLog, .position = last_position_};
        }

        if (file_.gcount() != static_cast<std::streamsize>(sizeof(header))) {
            return {.status = WalReadStatus::Failed, .error = WalError::IncompleteTrailingRecord, .position = last_position_};
        }

        if (!header.has_valid_static_fields()) {
            return {.status = WalReadStatus::Failed, .error = WalError::InvalidRecordHeader, .position = last_position_};
        }

        if (header.header_crc != calculate_record_header_crc(header)) {
            return {.status = WalReadStatus::Failed, .error = WalError::HeaderChecksumMismatch, .position = last_position_};
        }

        if (header.stream_id != segment_header_.stream_id || header.epoch != segment_header_.epoch) {
            return {.status = WalReadStatus::Failed, .error = WalError::InvalidRecordHeader, .position = last_position_};
        }

        const auto expected_sequence = last_position_.is_valid()
            ? last_position_.sequence + 1
            : segment_header_.first_sequence;
        if (header.sequence != expected_sequence) {
            return {.status = WalReadStatus::Failed, .error = WalError::UnexpectedSequence, .position = last_position_};
        }

        return {.status = WalReadStatus::RecordRead, .error = WalError::None, .position = {header.stream_id, header.epoch, header.sequence}};
    }

    WalReadResult WalSegmentReader::read_payload(const WalRecordHeader& header)
    {
        payload_buffer_.assign(header.payload_length, std::byte{});
        if (header.payload_length != 0) {
            file_.read(reinterpret_cast<char*>(payload_buffer_.data()), header.payload_length);
            if (file_.gcount() != static_cast<std::streamsize>(header.payload_length)) {
                return {.status = WalReadStatus::Failed, .error = WalError::IncompleteTrailingRecord, .position = last_position_};
            }
        }

        if (calculate_crc32(payload_buffer_) != header.payload_crc) {
            return {.status = WalReadStatus::Failed, .error = WalError::PayloadChecksumMismatch, .position = last_position_};
        }

        return {.status = WalReadStatus::RecordRead, .error = WalError::None, .position = {header.stream_id, header.epoch, header.sequence}};
    }
}
