#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_writer.hpp"
#include "wal/wal_file.hpp"

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

    void append_trailing_garbage(const std::filesystem::path& path)
    {
        std::ofstream file{path, std::ios::binary | std::ios::app};
        const std::array bytes{std::byte{0xAA}, std::byte{0xBB}};
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    void overwrite_payload_byte(const std::filesystem::path& path)
    {
        std::fstream file{path, std::ios::binary | std::ios::in | std::ios::out};
        file.seekp(static_cast<std::streamoff>(sizeof(wal::WalSegmentHeader) + sizeof(wal::WalRecordHeader)));
        const auto value = std::byte{0x7F};
        file.write(reinterpret_cast<const char*>(&value), 1);
    }
}

int main()
{
    const auto path = test_path("matching_engine_wal_writer_test.wal");
    std::filesystem::remove(path);

    const std::array payload{std::byte{1}, std::byte{2}, std::byte{3}};

    {
        wal::WalSegmentWriter writer{path, 7, 3, 10};
        const auto first = writer.append(200, payload);
        if (first.status != wal::WalRawAppendStatus::Appended || first.position.sequence != 10) {
            return 1;
        }

        const auto second = writer.append(200, payload);
        if (second.status != wal::WalRawAppendStatus::Appended || second.position.sequence != 11) {
            return 2;
        }

        if (writer.pending_count() != 2 || writer.committed_queue_size() != 0 || writer.has_committed_position()) {
            return 3;
        }

        const auto commit_result = writer.commit();
        if (commit_result.status != wal::WalCommitStatus::Committed || commit_result.committed_up_to.sequence != 11) {
            return 4;
        }

        wal::WalPosition committed;
        if (writer.pending_count() != 0 || writer.committed_queue_size() != 2) {
            return 5;
        }

        if (!writer.pop_committed_position(committed) || committed.sequence != 10) {
            return 6;
        }

        if (!writer.pop_committed_position(committed) || committed.sequence != 11) {
            return 7;
        }

        if (writer.pop_committed_position(committed)) {
            return 8;
        }
    }

    if (!wal::WalFile::exists(path) || wal::WalFile::size(path) <= sizeof(wal::WalSegmentHeader)) {
        return 9;
    }

    {
        wal::WalSegmentWriter reopened{path, 7, 3, 10};
        const auto result = reopened.append(200, payload);
        if (result.status != wal::WalRawAppendStatus::Appended || result.position.sequence != 12) {
            return 10;
        }
    }

    append_trailing_garbage(path);
    {
        wal::WalSegmentWriter recovered{path, 7, 3, 10};
        const auto result = recovered.append(200, payload);
        if (result.status != wal::WalRawAppendStatus::Appended || result.position.sequence != 13) {
            return 11;
        }
    }

    {
        wal::WalSegmentReader reader{path};
        wal::WalRecordView record;
        for (int expected = 10; expected <= 13; ++expected) {
            const auto result = reader.read_next(record);
            if (result.status != wal::WalReadStatus::RecordRead || record.header.sequence != static_cast<wal::SequenceNumber>(expected)) {
                return 12;
            }
        }
        if (reader.read_next(record).status != wal::WalReadStatus::EndOfLog) {
            return 13;
        }
    }

    const auto corrupted_path = test_path("matching_engine_wal_writer_corrupted_test.wal");
    std::filesystem::remove(corrupted_path);
    {
        wal::WalSegmentWriter writer{corrupted_path, 1, 1, 1};
        writer.append(200, payload);
        writer.commit();
    }
    overwrite_payload_byte(corrupted_path);
    {
        wal::WalSegmentWriter writer{corrupted_path, 1, 1, 1};
        const auto result = writer.append(200, payload);
        if (result.status != wal::WalRawAppendStatus::Failed || result.error != wal::WalError::CorruptedMiddleRecord) {
            return 14;
        }
    }

    const auto wrong_stream_path = test_path("matching_engine_wal_writer_wrong_stream_test.wal");
    std::filesystem::remove(wrong_stream_path);
    {
        wal::WalSegmentWriter writer{wrong_stream_path, 1, 1, 1};
        writer.append(200, payload);
        writer.commit();
    }
    {
        wal::WalSegmentWriter writer{wrong_stream_path, 2, 1, 1};
        const auto result = writer.append(200, payload);
        if (result.status != wal::WalRawAppendStatus::Failed || result.error != wal::WalError::InvalidSegmentHeader) {
            return 15;
        }
    }

    std::filesystem::remove(path);
    std::filesystem::remove(corrupted_path);
    std::filesystem::remove(wrong_stream_path);
    return 0;
}
