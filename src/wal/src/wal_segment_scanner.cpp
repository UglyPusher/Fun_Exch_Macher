#include "wal/wal_segment_scanner.hpp"

namespace wal {

WalSegmentScanResult WalSegmentScanner::scan(const std::filesystem::path&)
{
    return {.ok = true, .error = WalError::None};
}

} // namespace wal
