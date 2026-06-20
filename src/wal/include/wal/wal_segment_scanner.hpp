#pragma once

/**
 * @file wal_segment_scanner.hpp
 * @brief Recovery scanner for existing WAL segment files.
 *
 * The scanner finds the last valid record boundary and distinguishes a valid
 * torn tail from corruption in the middle of a segment.
 */

#include "wal/wal_result.hpp"

#include <filesystem>
#include <vector>

namespace wal
{
    /**
     * @brief Result of scanning a WAL segment for recovery.
     */
    struct WalSegmentScanResult
    {
        bool ok = false;
        WalError error = WalError::None;

        WalPosition last_valid_position {};
        std::uint64_t last_valid_offset = 0;

        bool has_incomplete_trailing_record = false;
    };

    /**
     * @brief Validates a segment without exposing domain payloads.
     */
    class WalSegmentScanner
    {
    public:
        /**
         * @brief Scans one file and reports the last safe append position.
         */
        WalSegmentScanResult scan(const std::filesystem::path& file_path);
    };
}
