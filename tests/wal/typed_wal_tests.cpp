#include "wal/typed_wal_reader.hpp"
#include "wal/typed_wal_writer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace
{
    struct TestRecord
    {
        std::uint64_t id = 0;
        std::uint32_t quantity = 0;
    };

    constexpr wal::RecordType test_record_type = 200;

    std::filesystem::path test_path(const char* name)
    {
        return std::filesystem::current_path() / name;
    }

    wal::WalConfig wal_config(const std::filesystem::path& path)
    {
        return {
            .path = path,
            .stream_id = 1,
            .epoch = 1,
            .first_sequence = 1
        };
    }
}

int main()
{
    const std::filesystem::path path = test_path("typed_wal_tests.wal");
    std::filesystem::remove(path);

    TestRecord written{.id = 42, .quantity = 7};
    wal::Wal wal_log{wal_config(path)};

    wal::TypedWalWriter<TestRecord, test_record_type> typed_writer{wal_log};
    if (!typed_writer.append(written).ok() || typed_writer.last_position().sequence != 1) {
        std::filesystem::remove(path);
        return 1;
    }

    wal::WalCursor cursor = wal_log.cursor_from_beginning();
    wal::TypedWalReader<TestRecord, test_record_type> typed_reader{wal_log, cursor};
    TestRecord read;
    if (typed_reader.read_next(read).status != wal::WalReadStatus::RecordRead
        || read.id != written.id
        || read.quantity != written.quantity) {
        std::filesystem::remove(path);
        return 2;
    }

    const std::array mismatch_payload{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    if (!wal_log.append(wal::WalMessageView{.record_type = 300, .payload = mismatch_payload}).ok()) {
        std::filesystem::remove(path);
        return 3;
    }

    wal::WalCursor mismatch_cursor = wal_log.cursor_from(2);
    wal::TypedWalReader<TestRecord, test_record_type> mismatch_reader{wal_log, mismatch_cursor};
    if (mismatch_reader.read_next(read).error != wal::WalError::RecordTypeMismatch) {
        std::filesystem::remove(path);
        return 4;
    }

    const std::array short_payload{std::byte{1}};
    if (!wal_log.append(wal::WalMessageView{.record_type = test_record_type, .payload = short_payload}).ok()) {
        std::filesystem::remove(path);
        return 5;
    }

    wal::WalCursor short_cursor = wal_log.cursor_from(3);
    wal::TypedWalReader<TestRecord, test_record_type> short_reader{wal_log, short_cursor};
    if (short_reader.read_next(read).error != wal::WalError::PayloadSizeMismatch) {
        std::filesystem::remove(path);
        return 6;
    }

    std::filesystem::remove(path);
    return 0;
}
