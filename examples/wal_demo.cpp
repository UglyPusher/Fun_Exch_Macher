/**
 * @file wal_demo.cpp
 * @brief Minimal standalone demonstration of the public WAL facade.
 *
 * The demo writes a few raw records, commits them, reads them back, and prints
 * a short summary. It intentionally uses only wal/wal.hpp.
 */

#include "wal/wal.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    constexpr wal::RecordType demo_record_type = 7001;
    constexpr wal::StreamId demo_stream_id = 501;
    constexpr wal::EpochId demo_epoch = 1;
    constexpr wal::SequenceNumber first_sequence = 1;

    std::vector<std::byte> bytes_from_text(std::string_view text)
    {
        const auto* first = reinterpret_cast<const std::byte*>(text.data());
        return {first, first + text.size()};
    }
}

int main(int argc, char** argv)
{
    const std::filesystem::path wal_path = argc > 1
        ? std::filesystem::path{argv[1]}
        : std::filesystem::path{"wal_demo.wal"};

    std::filesystem::remove(wal_path);

    const std::vector<std::vector<std::byte>> payloads{
        bytes_from_text("first demo record"),
        bytes_from_text("second demo record"),
        bytes_from_text("third demo record")
    };

    wal::WalWriter writer{wal::WalWriterConfig{
        .file_path = wal_path,
        .stream_id = demo_stream_id,
        .epoch = demo_epoch,
        .first_sequence = first_sequence
    }};

    for (const std::vector<std::byte>& payload : payloads) {
        const wal::WalAppendResult append_result = writer.append_record(demo_record_type, payload);
        if (append_result.status != wal::WalAppendStatus::Appended) {
            std::cerr << "wal_demo: append failed\n";
            return 1;
        }
    }

    const wal::WalCommitResult commit_result = writer.commit();
    if (commit_result.status != wal::WalCommitStatus::Committed) {
        std::cerr << "wal_demo: commit failed\n";
        return 2;
    }

    wal::WalReader reader{wal::WalReaderConfig{.file_path = wal_path}};
    std::uint64_t records_read = 0;
    std::uint64_t bytes_read = 0;

    while (true) {
        wal::WalRecord record;
        const wal::WalReadResult read_result = reader.read_next_record(record);
        if (read_result.status == wal::WalReadStatus::EndOfLog) {
            break;
        }
        if (read_result.status != wal::WalReadStatus::RecordRead) {
            std::cerr << "wal_demo: read failed\n";
            return 3;
        }

        ++records_read;
        bytes_read += record.payload.size();
    }

    std::cout << "wal_demo OK\n"
              << "path: " << wal_path << '\n'
              << "records_written: " << payloads.size() << '\n'
              << "records_read: " << records_read << '\n'
              << "payload_bytes_read: " << bytes_read << '\n'
              << "last_committed_sequence: " << commit_result.committed_up_to.sequence << '\n';
    return 0;
}
