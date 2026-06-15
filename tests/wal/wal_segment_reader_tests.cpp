#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_writer.hpp"

#include <array>
#include <cstddef>
#include <filesystem>

namespace
{
    std::filesystem::path test_path()
    {
        return std::filesystem::current_path() / "matching_engine_wal_reader_test.wal";
    }
}

int main()
{
    const auto path = test_path();
    std::filesystem::remove(path);

    const std::array first_payload{std::byte{1}, std::byte{2}};
    const std::array second_payload{std::byte{3}, std::byte{4}, std::byte{5}};

    {
        wal::WalSegmentWriter writer{path, 42, 9, 1};
        writer.append(200, first_payload);
        writer.append(300, second_payload);
        writer.commit();
    }

    wal::WalSegmentReader reader{path};
    wal::WalRecordView record;

    auto result = reader.read_next(record);
    if (result.status != wal::WalReadStatus::RecordRead || record.header.sequence != 1 || record.payload.size() != first_payload.size()) {
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

    std::filesystem::remove(path);
    return 0;
}
