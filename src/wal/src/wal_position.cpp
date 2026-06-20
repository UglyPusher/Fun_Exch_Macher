/**
 * @file wal_position.cpp
 * @brief Implements stream/epoch position checks.
 */

#include "wal/wal_position.hpp"

namespace wal
{
    bool WalPosition::is_valid() const noexcept
    {
        return stream_id != 0 && epoch != 0 && sequence != 0;
    }

    bool WalPosition::same_stream_epoch(const WalPosition& other) const noexcept
    {
        return stream_id == other.stream_id && epoch == other.epoch;
    }
}
