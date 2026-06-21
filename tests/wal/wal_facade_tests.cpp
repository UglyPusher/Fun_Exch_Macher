#include "wal/wal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
    constexpr wal::RecordType test_record_type = 9001;
    constexpr wal::StreamId test_stream_id = 42;
    constexpr wal::EpochId test_epoch = 7;
    constexpr wal::SequenceNumber first_sequence = 1;

    std::filesystem::path test_path(const char* name)
    {
        return std::filesystem::current_path() / name;
    }

    wal::WalWriterConfig writer_config(const std::filesystem::path& path)
    {
        return {
            .file_path = path,
            .stream_id = test_stream_id,
            .epoch = test_epoch,
            .first_sequence = first_sequence
        };
    }

    std::vector<std::byte> payload(std::initializer_list<unsigned char> bytes)
    {
        std::vector<std::byte> result;
        result.reserve(bytes.size());
        for (const unsigned char byte : bytes) {
            result.push_back(static_cast<std::byte>(byte));
        }
        return result;
    }

    bool same_payload(const std::vector<std::byte>& left, const std::vector<std::byte>& right)
    {
        return left == right;
    }

    bool write_payloads(
        const std::filesystem::path& path,
        const std::vector<std::vector<std::byte>>& payloads)
    {
        std::filesystem::remove(path);
        wal::WalWriter writer{writer_config(path)};

        for (std::size_t index = 0; index < payloads.size(); ++index) {
            const wal::WalAppendResult append_result = writer.append_record(test_record_type, payloads[index]);
            if (append_result.status != wal::WalAppendStatus::Appended
                || append_result.position.sequence != first_sequence + index) {
                return false;
            }
        }

        const wal::WalCommitResult commit_result = writer.commit();
        return commit_result.status == wal::WalCommitStatus::Committed;
    }

    std::vector<wal::WalRecord> read_all_records(const std::filesystem::path& path)
    {
        std::vector<wal::WalRecord> records;
        wal::WalReader reader{wal::WalReaderConfig{.file_path = path}};

        while (true) {
            wal::WalRecord record;
            const wal::WalReadResult read_result = reader.read_next_record(record);
            if (read_result.status == wal::WalReadStatus::EndOfLog) {
                return records;
            }
            if (read_result.status != wal::WalReadStatus::RecordRead) {
                records.clear();
                return records;
            }
            records.push_back(record);
        }
    }

    bool append_incomplete_tail(const std::filesystem::path& path)
    {
        std::ofstream file{path, std::ios::binary | std::ios::app};
        const std::array trailing_bytes{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
        file.write(reinterpret_cast<const char*>(trailing_bytes.data()), trailing_bytes.size());
        return static_cast<bool>(file);
    }

    bool corrupt_first_payload_byte(
        const std::filesystem::path& path,
        const std::vector<std::byte>& bytes_to_find)
    {
        std::ifstream input{path, std::ios::binary};
        std::vector<char> file_bytes{
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}
        };

        const auto payload_size = static_cast<std::ptrdiff_t>(bytes_to_find.size());
        for (auto position = file_bytes.begin(); position != file_bytes.end(); ++position) {
            if (std::distance(position, file_bytes.end()) < payload_size) {
                break;
            }

            bool found_payload = true;
            for (std::ptrdiff_t offset = 0; offset < payload_size; ++offset) {
                if (static_cast<std::byte>(position[offset]) != bytes_to_find[static_cast<std::size_t>(offset)]) {
                    found_payload = false;
                    break;
                }
            }

            if (found_payload) {
                std::fstream output{path, std::ios::binary | std::ios::in | std::ios::out};
                const std::uint64_t byte_offset = static_cast<std::uint64_t>(std::distance(file_bytes.begin(), position));
                output.seekp(static_cast<std::streamoff>(byte_offset));
                const std::byte corrupted_value{0x7F};
                output.write(reinterpret_cast<const char*>(&corrupted_value), 1);
                return static_cast<bool>(output);
            }
        }

        return false;
    }

    bool WalFacade_write_read_one_record()
    {
        const auto path = test_path("wal_facade_one_record.wal");
        const std::vector<std::byte> expected_payload = payload({1, 2, 3, 4});

        if (!write_payloads(path, {expected_payload})) {
            return false;
        }

        const std::vector<wal::WalRecord> records = read_all_records(path);
        std::filesystem::remove(path);
        return records.size() == 1
            && records[0].record_type == test_record_type
            && records[0].position.sequence == 1
            && same_payload(records[0].payload, expected_payload);
    }

    bool WalFacade_write_read_many_records()
    {
        const auto path = test_path("wal_facade_many_records.wal");
        const std::vector<std::vector<std::byte>> expected_payloads{
            payload({1}),
            payload({2, 3}),
            payload({4, 5, 6}),
            payload({7, 8, 9, 10})
        };

        if (!write_payloads(path, expected_payloads)) {
            return false;
        }

        const std::vector<wal::WalRecord> records = read_all_records(path);
        std::filesystem::remove(path);
        if (records.size() != expected_payloads.size()) {
            return false;
        }

        for (std::size_t index = 0; index < records.size(); ++index) {
            if (records[index].record_type != test_record_type
                || records[index].position.sequence != first_sequence + index
                || !same_payload(records[index].payload, expected_payloads[index])) {
                return false;
            }
        }
        return true;
    }

    bool WalFacade_empty_wal_reads_end_of_log()
    {
        const auto path = test_path("wal_facade_empty.wal");
        std::filesystem::remove(path);

        {
            wal::WalWriter writer{writer_config(path)};
            const wal::WalCommitResult commit_result = writer.commit();
            if (commit_result.status != wal::WalCommitStatus::Committed) {
                return false;
            }
        }

        wal::WalReader reader{wal::WalReaderConfig{.file_path = path}};
        wal::WalRecord record;
        const wal::WalReadResult read_result = reader.read_next_record(record);
        std::filesystem::remove(path);
        return read_result.status == wal::WalReadStatus::EndOfLog;
    }

    bool WalFacade_recovers_incomplete_trailing_record()
    {
        const auto path = test_path("wal_facade_incomplete_tail.wal");
        const std::vector<std::byte> expected_payload = payload({11, 12, 13});
        if (!write_payloads(path, {expected_payload}) || !append_incomplete_tail(path)) {
            return false;
        }

        const wal::WalRecoveryResult scan_result = wal::scan_wal_segment(path);
        if (scan_result.ok || scan_result.error != wal::WalError::IncompleteTrailingRecord) {
            return false;
        }

        const wal::WalRecoveryResult recovery_result = wal::recover_wal_segment(path);
        if (!recovery_result.ok || !recovery_result.recovered_incomplete_tail) {
            return false;
        }

        const std::vector<wal::WalRecord> records = read_all_records(path);
        std::filesystem::remove(path);
        return records.size() == 1 && same_payload(records[0].payload, expected_payload);
    }

    bool WalFacade_rejects_corrupted_middle_record()
    {
        const auto path = test_path("wal_facade_corrupted_middle.wal");
        const std::vector<std::byte> first_payload = payload({21, 22, 23, 24, 25});
        const std::vector<std::byte> second_payload = payload({31, 32, 33});
        if (!write_payloads(path, {first_payload, second_payload})
            || !corrupt_first_payload_byte(path, first_payload)) {
            return false;
        }

        const wal::WalRecoveryResult scan_result = wal::scan_wal_segment(path);
        std::filesystem::remove(path);
        return !scan_result.ok
            && scan_result.error == wal::WalError::CorruptedMiddleRecord
            && !scan_result.recovered_incomplete_tail;
    }

    bool WalFacade_failed_writer_is_not_reusable()
    {
        const auto directory_path = test_path("wal_facade_writer_failure_directory");
        std::filesystem::remove_all(directory_path);
        std::filesystem::create_directory(directory_path);

        wal::WalWriter writer{writer_config(directory_path)};
        const std::vector<std::byte> bytes = payload({1, 2, 3});

        const wal::WalAppendResult first_append = writer.append_record(test_record_type, bytes);
        const wal::WalAppendResult second_append = writer.append_record(test_record_type, bytes);
        const wal::WalCommitResult commit_result = writer.commit();

        std::filesystem::remove_all(directory_path);
        return first_append.status == wal::WalAppendStatus::Failed
            && second_append.status == wal::WalAppendStatus::Failed
            && commit_result.status == wal::WalCommitStatus::Failed;
    }
}

int main()
{
    if (!WalFacade_write_read_one_record()) {
        return 1;
    }
    if (!WalFacade_write_read_many_records()) {
        return 2;
    }
    if (!WalFacade_empty_wal_reads_end_of_log()) {
        return 3;
    }
    if (!WalFacade_recovers_incomplete_trailing_record()) {
        return 4;
    }
    if (!WalFacade_rejects_corrupted_middle_record()) {
        return 5;
    }
    if (!WalFacade_failed_writer_is_not_reusable()) {
        return 6;
    }

    return 0;
}
