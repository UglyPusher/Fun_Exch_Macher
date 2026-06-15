#include "wal/wal_segment_scanner.hpp"
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

    void write_one_record(const std::filesystem::path& path)
    {
        std::filesystem::remove(path);
        wal::WalSegmentWriter writer{path, 1, 1, 1};
        const std::array payload{std::byte{1}, std::byte{2}, std::byte{3}};
        writer.append(200, payload);
        writer.commit();
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
    const auto path = test_path("matching_engine_wal_recovery_test.wal");
    write_one_record(path);

    wal::WalSegmentScanner scanner;
    auto result = scanner.scan(path);
    if (!result.ok || result.last_valid_position.sequence != 1 || result.last_valid_offset == 0) {
        return 1;
    }

    {
        std::ofstream file{path, std::ios::binary | std::ios::app};
        const std::array partial{std::byte{0xAA}, std::byte{0xBB}};
        file.write(reinterpret_cast<const char*>(partial.data()), partial.size());
    }

    result = scanner.scan(path);
    if (result.ok || result.error != wal::WalError::IncompleteTrailingRecord || !result.has_incomplete_trailing_record) {
        return 2;
    }

    const auto bad_payload_path = test_path("matching_engine_wal_recovery_bad_payload_test.wal");
    write_one_record(bad_payload_path);
    overwrite_byte(bad_payload_path, sizeof(wal::WalSegmentHeader) + sizeof(wal::WalRecordHeader), std::byte{0x7F});
    result = scanner.scan(bad_payload_path);
    if (result.ok || result.error != wal::WalError::CorruptedMiddleRecord || result.has_incomplete_trailing_record) {
        return 3;
    }

    const auto bad_segment_path = test_path("matching_engine_wal_recovery_bad_segment_test.wal");
    write_one_record(bad_segment_path);
    overwrite_byte(bad_segment_path, offsetof(wal::WalSegmentHeader, epoch), std::byte{0x7F});
    result = scanner.scan(bad_segment_path);
    if (result.ok || result.error != wal::WalError::HeaderChecksumMismatch) {
        return 4;
    }

    std::filesystem::remove(path);
    std::filesystem::remove(bad_payload_path);
    std::filesystem::remove(bad_segment_path);
    return 0;
}
