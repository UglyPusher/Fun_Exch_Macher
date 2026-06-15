#pragma once

#include "wal/raw_wal_writer.hpp"
#include "wal/wal_record_header.hpp"
#include "wal/wal_segment_header.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

namespace wal
{
    class WalSegmentWriter final : public RawWalWriter
    {
    public:
        WalSegmentWriter(
            std::filesystem::path file_path,
            StreamId stream_id,
            EpochId epoch,
            SequenceNumber first_sequence);

        ~WalSegmentWriter() override;

        WalAppendResult append(
            RecordType record_type,
            std::span<const std::byte> payload) override;

        WalCommitResult commit() override;

        [[nodiscard]] WalPosition last_position() const noexcept override;

    private:
        WalAppendResult write_record(
            RecordType record_type,
            std::span<const std::byte> payload);

        [[nodiscard]] WalRecordHeader make_header(
            RecordType record_type,
            std::span<const std::byte> payload,
            SequenceNumber sequence) const;

        void write_segment_header();

    private:
        std::filesystem::path file_path_;
        std::ofstream file_;

        StreamId stream_id_ = 0;
        EpochId epoch_ = 0;
        SequenceNumber next_sequence_ = 1;
        WalPosition last_position_ {};

        std::vector<std::byte> write_buffer_;
    };
}
