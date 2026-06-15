#include "wal/wal_segment_writer.hpp"
#include "wal/wal_file.hpp"

#include <array>
#include <cstddef>
#include <filesystem>

namespace
{
    std::filesystem::path test_path()
    {
        return std::filesystem::current_path() / "matching_engine_wal_writer_test.wal";
    }
}

int main()
{
    const auto path = test_path();
    std::filesystem::remove(path);

    wal::WalSegmentWriter writer{path, 7, 3, 10};
    const std::array payload{std::byte{1}, std::byte{2}, std::byte{3}};

    const auto first = writer.append(200, payload);
    if (first.status != wal::WalAppendStatus::Appended || first.position.sequence != 10) {
        return 1;
    }

    const auto second = writer.append(200, payload);
    if (second.status != wal::WalAppendStatus::Appended || second.position.sequence != 11) {
        return 2;
    }

    if (writer.commit().status != wal::WalCommitStatus::Committed) {
        return 3;
    }

    if (!wal::WalFile::exists(path) || wal::WalFile::size(path) <= sizeof(wal::WalSegmentHeader)) {
        return 4;
    }

    std::filesystem::remove(path);
    return 0;
}
