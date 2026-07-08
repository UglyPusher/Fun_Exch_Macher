/**
 * @file wal_result.cpp
 * @brief Translation unit for WAL result types.
 */

#include "wal/wal_result.hpp"

namespace wal
{
    bool WalReadResult::ok() const noexcept
    {
        return status == WalReadStatus::RecordRead;
    }
}
