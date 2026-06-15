#include "wal/wal_segment_scanner.hpp"
#include "wal/wal_segment_writer.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>

namespace
{
    std::filesystem::path test_path()
    {
        return std::filesystem::current_path() / "matching_engine_wal_recovery_test.wal";
    }
}

int main()
{
    const auto path = test_path();
    std::filesystem::remove(path);

    {
        wal::WalSegmentWriter writer{path, 1, 1, 1};
        const std::array payload{std::byte{1}, std::byte{2}, std::byte{3}};
        writer.append(200, payload);
        writer.commit();
    }

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

    std::filesystem::remove(path);
    return 0;
}
