#include "wal/wal_segment_scanner.hpp"

#include "wal/wal_checksum.hpp"
#include "wal/wal_record_header.hpp"
#include "wal/wal_segment_header.hpp"

#include <fstream>
#include <vector>

namespace wal
{
    WalSegmentScanResult WalSegmentScanner::scan(const std::filesystem::path& file_path)
    {
        std::ifstream file{file_path, std::ios::binary};
        if (!file) {
            return {.ok = false, .error = WalError::CannotOpenFile};
        }

        WalSegmentHeader segment_header;
        file.read(reinterpret_cast<char*>(&segment_header), sizeof(segment_header));
        if (file.gcount() != static_cast<std::streamsize>(sizeof(segment_header)) || !segment_header.has_valid_static_fields()) {
            return {.ok = false, .error = WalError::InvalidSegmentHeader};
        }

        if (segment_header.header_crc != calculate_segment_header_crc(segment_header)) {
            return {.ok = false, .error = WalError::HeaderChecksumMismatch};
        }

        WalSegmentScanResult result{
            .ok = true,
            .error = WalError::None,
            .last_valid_offset = sizeof(WalSegmentHeader)
        };

        SequenceNumber expected_sequence = segment_header.first_sequence;
        while (true) {
            const auto record_offset = static_cast<std::uint64_t>(file.tellg());

            WalRecordHeader header;
            file.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (file.gcount() == 0 && file.eof()) {
                result.last_valid_offset = record_offset;
                return result;
            }

            if (file.gcount() != static_cast<std::streamsize>(sizeof(header))) {
                result.ok = false;
                result.error = WalError::IncompleteTrailingRecord;
                result.has_incomplete_trailing_record = true;
                result.last_valid_offset = record_offset;
                return result;
            }

            if (!header.has_valid_static_fields()
                || header.header_crc != calculate_record_header_crc(header)
                || header.stream_id != segment_header.stream_id
                || header.epoch != segment_header.epoch
                || header.sequence != expected_sequence) {
                result.ok = false;
                result.error = WalError::CorruptedMiddleRecord;
                result.last_valid_offset = record_offset;
                return result;
            }

            std::vector<std::byte> payload(header.payload_length);
            if (header.payload_length != 0) {
                file.read(reinterpret_cast<char*>(payload.data()), header.payload_length);
                if (file.gcount() != static_cast<std::streamsize>(header.payload_length)) {
                    result.ok = false;
                    result.error = WalError::IncompleteTrailingRecord;
                    result.has_incomplete_trailing_record = true;
                    result.last_valid_offset = record_offset;
                    return result;
                }
            }

            if (calculate_crc32(payload) != header.payload_crc) {
                result.ok = false;
                result.error = WalError::CorruptedMiddleRecord;
                result.last_valid_offset = record_offset;
                return result;
            }

            const auto padding = header.record_length - sizeof(WalRecordHeader) - header.payload_length;
            if (padding != 0) {
                file.ignore(static_cast<std::streamsize>(padding));
                if (!file) {
                    result.ok = false;
                    result.error = WalError::IncompleteTrailingRecord;
                    result.has_incomplete_trailing_record = true;
                    result.last_valid_offset = record_offset;
                    return result;
                }
            }

            result.last_valid_position = {header.stream_id, header.epoch, header.sequence};
            result.last_valid_offset = static_cast<std::uint64_t>(file.tellg());
            ++expected_sequence;
        }
    }
}
