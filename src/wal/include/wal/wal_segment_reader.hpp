#pragma once

/**
 * @file wal_segment_reader.hpp
 * @brief Validating reader for one binary WAL segment file.
 *
 * The reader exposes records only after segment header, record header, payload
 * checksum, and sequence checks pass. It must not interpret payload bytes as
 * domain records.
 */

#include "wal/raw_wal_reader.hpp"
#include "wal/wal_segment_header.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

namespace wal
{
    /**
     * @brief Sequential raw reader for one WAL segment.
     */
    class WalSegmentReader final : public RawWalReader
    {
    public:
        /**
         * @brief Opens a segment file and prepares validation state.
         */
        explicit WalSegmentReader(std::filesystem::path file_path);

        /**
         * @brief Reads the next validated record or reports end/failure.
         */
        WalReadResult read_next(WalRecordView& out) override;

        /**
         * @brief Returns the last successfully read position.
         */
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
        WalReadResult open_result_ {.status = WalReadStatus::Failed, .error = WalError::CannotReadFile};

        std::vector<std::byte> payload_buffer_;
    };
}
