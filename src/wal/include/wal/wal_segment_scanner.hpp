#pragma once

#include "wal/wal_result.hpp"

#include <filesystem>
#include <vector>

namespace wal
{
    struct WalSegmentScanResult
    {
        bool ok = false;
        WalError error = WalError::None;

        WalPosition last_valid_position {};
        std::uint64_t last_valid_offset = 0;

        bool has_incomplete_trailing_record = false;
    };

    class WalSegmentScanner
    {
    public:
        WalSegmentScanResult scan(const std::filesystem::path& file_path);
    };
}