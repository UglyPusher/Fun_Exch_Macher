#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_writer.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>

namespace
{
    std::filesystem::path test_path(const char* name)
    {
        return std::filesystem::current_path() / name;
    }

    void write_two_records(const std::filesystem::path& path)
    {
        std::filesystem::remove(path);
        const std::array first_payload{std::byte{1}, std::byte{2}};
        const std::array second_payload{std::byte{3}, std::byte{4}, std::byte{5}};

        wal::WalSegmentWriter writer{path, 42, 9, 1};
        writer.append(200, first_payload);
        writer.append(300, second_payload);
        writer.flush_pending_writes();
    }

    void overwrite_byte(const std::filesystem::path& path, std::uint64_t offset, std::byte value)
    {
        std::fstream file{path, std::ios::binary | std::ios::in | std::ios::out};
        file.seekp(static_cast<std::streamoff>(offset));
        file.write(reinterpret_cast<const char*>(&value), 1);
    }
}

int main()
{
    const auto path = test_path("matching_engine_wal_reader_test.wal");
    write_two_records(path);

    wal::WalSegmentReader reader{path};
    wal::WalRecordView record;

    auto result = reader.read_next(record);
    if (result.status != wal::WalReadStatus::RecordRead || record.header.sequence != 1 || record.payload.size() != 2) {
        return 1;
    }

    result = reader.read_next(record);
    if (result.status != wal::WalReadStatus::RecordRead || record.header.sequence != 2 || record.header.record_type != 300) {
        return 2;
    }

    result = reader.read_next(record);
    if (result.status != wal::WalReadStatus::EndOfLog) {
        return 3;
    }

    const auto bad_segment_path = test_path("matching_engine_wal_bad_segment_header_test.wal");
    write_two_records(bad_segment_path);
    overwrite_byte(bad_segment_path, offsetof(wal::WalSegmentHeader, first_sequence), std::byte{0x7F});
    wal::WalSegmentReader bad_segment_reader{bad_segment_path};
    if (bad_segment_reader.read_next(record).error != wal::WalError::HeaderChecksumMismatch) {
        return 4;
    }

    const auto bad_record_header_path = test_path("matching_engine_wal_bad_record_header_test.wal");
    write_two_records(bad_record_header_path);
    overwrite_byte(
        bad_record_header_path,
        sizeof(wal::WalSegmentHeader) + offsetof(wal::WalRecordHeader, record_type),
        std::byte{0x7F});
    wal::WalSegmentReader bad_record_header_reader{bad_record_header_path};
    if (bad_record_header_reader.read_next(record).error != wal::WalError::HeaderChecksumMismatch) {
        return 5;
    }

    const auto bad_payload_path = test_path("matching_engine_wal_bad_payload_test.wal");
    write_two_records(bad_payload_path);
    overwrite_byte(bad_payload_path, sizeof(wal::WalSegmentHeader) + sizeof(wal::WalRecordHeader), std::byte{0x7F});
    wal::WalSegmentReader bad_payload_reader{bad_payload_path};
    if (bad_payload_reader.read_next(record).error != wal::WalError::PayloadChecksumMismatch) {
        return 6;
    }

    const auto bad_sequence_path = test_path("matching_engine_wal_bad_sequence_test.wal");
    write_two_records(bad_sequence_path);
    wal::WalRecordHeader header;
    {
        std::fstream file{bad_sequence_path, std::ios::binary | std::ios::in | std::ios::out};
        file.seekg(sizeof(wal::WalSegmentHeader));
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        header.sequence = 99;
        header.header_crc = wal::calculate_record_header_crc(header);
        file.seekp(sizeof(wal::WalSegmentHeader));
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }
    wal::WalSegmentReader bad_sequence_reader{bad_sequence_path};
    if (bad_sequence_reader.read_next(record).error != wal::WalError::UnexpectedSequence) {
        return 7;
    }

    std::filesystem::remove(path);
    std::filesystem::remove(bad_segment_path);
    std::filesystem::remove(bad_record_header_path);
    std::filesystem::remove(bad_payload_path);
    std::filesystem::remove(bad_sequence_path);
    return 0;
}
