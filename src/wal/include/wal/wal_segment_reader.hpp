#pragma once

#include "wal/raw_wal_reader.hpp"
#include "wal/wal_segment_header.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

namespace wal
{
    class WalSegmentReader final : public RawWalReader
    {
    public:
        explicit WalSegmentReader(std::filesystem::path file_path);

        WalReadResult read_next(WalRecordView& out) override;

        [[nodiscard]] WalPosition last_position() const noexcept override;

    private:
        WalReadResult read_segment_header();
        WalReadResult read_record_header(WalRecordHeader& header);
        WalReadResult read_payload(const WalRecordHeader& header);

    private:
        std::filesystem::path file_path_;
        std::ifstream file_;

        WalSegmentHeader segment_header_ {};
        WalPosition last_position_ {};

        std::vector<std::byte> payload_buffer_;
    };
}