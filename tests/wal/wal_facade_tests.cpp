#include "wal/wal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    constexpr wal::RecordType test_record_type = 9001;
    constexpr wal::StreamId test_stream_id = 42;
    constexpr wal::EpochId test_epoch = 7;
    constexpr wal::WalSeq first_sequence = 1;

    std::filesystem::path test_path(const char* name)
    {
        return std::filesystem::current_path() / name;
    }

    wal::WalConfig wal_config(const std::filesystem::path& path)
    {
        return {
            .path = path,
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

    wal::WalMessageView message_view(const std::vector<std::byte>& payload)
    {
        return {.record_type = test_record_type, .payload = payload};
    }

    bool same_payload(const std::vector<std::byte>& left, const std::vector<std::byte>& right)
    {
        return left == right;
    }

    bool append_incomplete_tail(const std::filesystem::path& path)
    {
        std::ofstream file{path, std::ios::binary | std::ios::app};
        const std::array trailing_bytes{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
        file.write(reinterpret_cast<const char*>(trailing_bytes.data()), trailing_bytes.size());
        return static_cast<bool>(file);
    }

    bool truncate_file_tail(const std::filesystem::path& path, std::uint64_t byte_count_to_remove)
    {
        const std::uint64_t current_size = std::filesystem::file_size(path);
        if (current_size < byte_count_to_remove) {
            return false;
        }

        std::filesystem::resize_file(path, current_size - byte_count_to_remove);
        return true;
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

    bool append_one_record_then_read_it()
    {
        const auto path = test_path("wal_facade_one_record.wal");
        std::filesystem::remove(path);

        const std::vector<std::byte> expected_payload = payload({1, 2, 3, 4});
        wal::Wal wal_log{wal_config(path)};

        const wal::WalAppendResult append_result = wal_log.append(message_view(expected_payload));
        if (!append_result.ok() || append_result.position.sequence != first_sequence) {
            std::filesystem::remove(path);
            return false;
        }

        wal::WalCursor cursor = wal_log.cursor_from_beginning();
        wal::WalRecord record;
        const wal::WalReadResult read_result = wal_log.read_next(cursor, record);
        const bool passed = read_result.ok()
            && record.record_type == test_record_type
            && record.position.sequence == first_sequence
            && same_payload(record.payload, expected_payload)
            && wal_log.get_message_state(first_sequence) == wal::WalMessageState::Committed;

        std::filesystem::remove(path);
        return passed;
    }

    bool append_batch_then_read_all()
    {
        const auto path = test_path("wal_facade_batch.wal");
        std::filesystem::remove(path);

        const std::vector<std::vector<std::byte>> expected_payloads{
            payload({1}),
            payload({2, 3}),
            payload({4, 5, 6})
        };
        const std::array messages{
            message_view(expected_payloads[0]),
            message_view(expected_payloads[1]),
            message_view(expected_payloads[2])
        };

        wal::Wal wal_log{wal_config(path)};
        const wal::WalBatchAppendResult append_result = wal_log.append_batch(messages);
        if (!append_result.ok() || append_result.messages_committed != messages.size()) {
            std::filesystem::remove(path);
            return false;
        }

        wal::WalCursor cursor = wal_log.cursor_from_beginning();
        std::array<wal::WalRecord, 3> records;
        const wal::WalBatchReadResult read_result = wal_log.read_batch(cursor, records);
        if (!read_result.ok() || read_result.records_read != records.size()) {
            std::filesystem::remove(path);
            return false;
        }

        for (std::size_t index = 0; index < records.size(); ++index) {
            const wal::WalSeq expected_sequence = first_sequence + index;
            if (records[index].position.sequence != expected_sequence
                || !same_payload(records[index].payload, expected_payloads[index])
                || wal_log.get_message_state(expected_sequence) != wal::WalMessageState::Committed) {
                std::filesystem::remove(path);
                return false;
            }
        }

        std::filesystem::remove(path);
        return true;
    }

    bool read_does_not_return_uncommitted_or_incomplete_tail()
    {
        const auto path = test_path("wal_facade_incomplete_tail.wal");
        std::filesystem::remove(path);

        const std::vector<std::byte> expected_payload = payload({11, 12, 13});
        {
            wal::Wal wal_log{wal_config(path)};
            if (!wal_log.append(message_view(expected_payload)).ok()) {
                std::filesystem::remove(path);
                return false;
            }
        }

        if (!append_incomplete_tail(path)) {
            std::filesystem::remove(path);
            return false;
        }

        wal::Wal recovered_wal{wal_config(path)};
        wal::WalCursor cursor = recovered_wal.cursor_from_beginning();
        wal::WalRecord record;
        const wal::WalReadResult first_read = recovered_wal.read_next(cursor, record);
        const wal::WalReadResult second_read = recovered_wal.read_next(cursor, record);

        const bool passed = first_read.ok()
            && same_payload(record.payload, expected_payload)
            && second_read.status == wal::WalReadStatus::EndOfLog;

        std::filesystem::remove(path);
        return passed;
    }

    bool batch_visibility_is_atomic()
    {
        const auto path = test_path("wal_facade_atomic_batch.wal");
        std::filesystem::remove(path);

        const std::vector<std::byte> first_payload = payload({21});
        const std::vector<std::byte> second_payload = payload({22, 23});
        const std::vector<std::byte> third_payload = payload({24, 25, 26});
        {
            wal::Wal wal_log{wal_config(path)};
            const std::array messages{
                message_view(first_payload),
                message_view(second_payload),
                message_view(third_payload)
            };
            if (!wal_log.append_batch(messages).ok()) {
                std::filesystem::remove(path);
                return false;
            }
        }

        if (!truncate_file_tail(path, 4)) {
            std::filesystem::remove(path);
            return false;
        }

        wal::Wal recovered_wal{wal_config(path)};
        wal::WalCursor cursor = recovered_wal.cursor_from_beginning();
        wal::WalRecord record;
        const wal::WalReadResult read_result = recovered_wal.read_next(cursor, record);

        std::filesystem::remove(path);
        return read_result.status == wal::WalReadStatus::EndOfLog;
    }

    bool append_after_recovery_continues_sequence()
    {
        const auto path = test_path("wal_facade_append_after_recovery.wal");
        std::filesystem::remove(path);

        const std::vector<std::byte> first_payload = payload({31});
        const std::vector<std::byte> second_payload = payload({32});
        const std::vector<std::byte> third_payload = payload({33});

        {
            wal::Wal wal_log{wal_config(path)};
            if (!wal_log.append(message_view(first_payload)).ok()
                || !wal_log.append(message_view(second_payload)).ok()) {
                std::filesystem::remove(path);
                return false;
            }
        }

        wal::Wal recovered_wal{wal_config(path)};
        const wal::WalAppendResult append_result = recovered_wal.append(message_view(third_payload));
        if (!append_result.ok() || append_result.position.sequence != first_sequence + 2) {
            std::filesystem::remove(path);
            return false;
        }

        wal::WalCursor cursor = recovered_wal.cursor_from_beginning();
        std::array<wal::WalRecord, 3> records;
        const wal::WalBatchReadResult read_result = recovered_wal.read_batch(cursor, records);

        const bool passed = read_result.records_read == 3
            && same_payload(records[0].payload, first_payload)
            && same_payload(records[1].payload, second_payload)
            && same_payload(records[2].payload, third_payload);

        std::filesystem::remove(path);
        return passed;
    }

    bool payload_too_large_is_rejected()
    {
        const auto path = test_path("wal_facade_too_large.wal");
        std::filesystem::remove(path);

        wal::Wal wal_log{wal_config(path)};
        const auto* fake_payload = static_cast<const std::byte*>(nullptr);
        const std::span<const std::byte> impossible_payload{
            fake_payload,
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1U
        };

        const wal::WalAppendResult append_result = wal_log.append(wal::WalMessageView{
            .record_type = test_record_type,
            .payload = impossible_payload
        });

        const bool passed = append_result.status == wal::WalAppendStatus::Rejected
            && append_result.error == wal::WalError::InvalidPayloadLength
            && wal_log.get_message_state(first_sequence) == wal::WalMessageState::Failed;

        std::filesystem::remove(path);
        return passed;
    }

    bool corrupted_middle_is_detected()
    {
        const auto path = test_path("wal_facade_corrupted_middle.wal");
        std::filesystem::remove(path);

        const std::vector<std::byte> first_payload = payload({41, 42, 43, 44});
        const std::vector<std::byte> second_payload = payload({45, 46});
        {
            wal::Wal wal_log{wal_config(path)};
            if (!wal_log.append(message_view(first_payload)).ok()
                || !wal_log.append(message_view(second_payload)).ok()) {
                std::filesystem::remove(path);
                return false;
            }
        }

        if (!corrupt_first_payload_byte(path, first_payload)) {
            std::filesystem::remove(path);
            return false;
        }

        wal::Wal corrupted_wal{wal_config(path)};
        wal::WalCursor cursor = corrupted_wal.cursor_from_beginning();
        wal::WalRecord record;
        const wal::WalReadResult read_result = corrupted_wal.read_next(cursor, record);

        std::filesystem::remove(path);
        return read_result.status == wal::WalReadStatus::Failed
            && read_result.error == wal::WalError::CorruptedMiddleRecord;
    }

    bool read_batch_respects_buffer_size()
    {
        const auto path = test_path("wal_facade_batch_buffer.wal");
        std::filesystem::remove(path);

        wal::Wal wal_log{wal_config(path)};
        std::vector<std::vector<std::byte>> payloads;
        std::vector<wal::WalMessageView> messages;
        for (unsigned char value = 0; value < 10; ++value) {
            payloads.push_back(payload({value}));
            messages.push_back(message_view(payloads.back()));
        }

        if (!wal_log.append_batch(messages).ok()) {
            std::filesystem::remove(path);
            return false;
        }

        wal::WalCursor cursor = wal_log.cursor_from_beginning();
        std::array<wal::WalRecord, 3> records;
        const std::array<std::size_t, 5> expected_reads{3, 3, 3, 1, 0};

        for (const std::size_t expected_read_count : expected_reads) {
            const wal::WalBatchReadResult read_result = wal_log.read_batch(cursor, records);
            if (read_result.records_read != expected_read_count) {
                std::filesystem::remove(path);
                return false;
            }
        }

        std::filesystem::remove(path);
        return true;
    }
}

int main()
{
    if (!append_one_record_then_read_it()) {
        return 1;
    }
    if (!append_batch_then_read_all()) {
        return 2;
    }
    if (!read_does_not_return_uncommitted_or_incomplete_tail()) {
        return 3;
    }
    if (!batch_visibility_is_atomic()) {
        return 4;
    }
    if (!append_after_recovery_continues_sequence()) {
        return 5;
    }
    if (!payload_too_large_is_rejected()) {
        return 6;
    }
    if (!corrupted_middle_is_detected()) {
        return 7;
    }
    if (!read_batch_respects_buffer_size()) {
        return 8;
    }

    return 0;
}
