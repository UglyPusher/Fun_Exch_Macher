#include "wal/typed_wal_reader.hpp"
#include "wal/typed_wal_writer.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace
{
    struct TestRecord
    {
        std::uint64_t id = 0;
        std::uint32_t quantity = 0;
    };

    class MemoryWriter final : public wal::RawWalWriter
    {
    public:
        wal::WalAppendResult append(wal::RecordType record_type, std::span<const std::byte> payload) override
        {
            record_type_ = record_type;
            payload_.assign(payload.begin(), payload.end());
            last_position_ = {1, 1, 1};
            return {.status = wal::WalAppendStatus::Appended, .error = wal::WalError::None, .position = last_position_};
        }

        wal::WalCommitResult commit() override
        {
            return {.status = wal::WalCommitStatus::Committed, .error = wal::WalError::None, .committed_up_to = last_position_};
        }

        [[nodiscard]] wal::WalPosition last_position() const noexcept override
        {
            return last_position_;
        }

        wal::RecordType record_type_ = 0;
        std::vector<std::byte> payload_;
        wal::WalPosition last_position_{};
    };

    class MemoryReader final : public wal::RawWalReader
    {
    public:
        MemoryReader(wal::RecordType record_type, std::span<const std::byte> payload)
            : record_type_(record_type), payload_(payload.begin(), payload.end())
        {
        }

        wal::WalReadResult read_next(wal::WalRecordView& out) override
        {
            if (consumed_) {
                return {.status = wal::WalReadStatus::EndOfLog, .error = wal::WalError::EndOfLog, .position = last_position_};
            }

            consumed_ = true;
            last_position_ = {1, 1, 1};
            out.header = {.record_length = static_cast<std::uint32_t>(sizeof(wal::WalRecordHeader) + payload_.size()), .record_type = record_type_, .stream_id = 1, .epoch = 1, .sequence = 1, .payload_length = static_cast<std::uint32_t>(payload_.size())};
            out.payload = std::span<const std::byte>{payload_.data(), payload_.size()};
            return {.status = wal::WalReadStatus::RecordRead, .error = wal::WalError::None, .position = last_position_};
        }

        [[nodiscard]] wal::WalPosition last_position() const noexcept override
        {
            return last_position_;
        }

        wal::RecordType record_type_ = 0;
        std::vector<std::byte> payload_;
        bool consumed_ = false;
        wal::WalPosition last_position_{};
    };
}

int main()
{
    TestRecord written{.id = 42, .quantity = 7};

    MemoryWriter raw_writer;
    wal::TypedWalWriter<TestRecord, 200> typed_writer{raw_writer};
    if (typed_writer.append(written).status != wal::WalAppendStatus::Appended || raw_writer.record_type_ != 200) {
        return 1;
    }

    MemoryReader raw_reader{200, raw_writer.payload_};
    wal::TypedWalReader<TestRecord, 200> typed_reader{raw_reader};
    TestRecord read;
    if (typed_reader.read_next(read).status != wal::WalReadStatus::RecordRead || read.id != written.id || read.quantity != written.quantity) {
        return 2;
    }

    MemoryReader mismatch_reader{300, raw_writer.payload_};
    wal::TypedWalReader<TestRecord, 200> mismatch_typed_reader{mismatch_reader};
    if (mismatch_typed_reader.read_next(read).error != wal::WalError::RecordTypeMismatch) {
        return 3;
    }

    const std::array short_payload{std::byte{1}};
    MemoryReader short_reader{200, short_payload};
    wal::TypedWalReader<TestRecord, 200> short_typed_reader{short_reader};
    if (short_typed_reader.read_next(read).error != wal::WalError::PayloadSizeMismatch) {
        return 4;
    }

    return 0;
}
