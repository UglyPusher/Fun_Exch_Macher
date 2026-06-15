#pragma once

#include "wal/wal_types.hpp"

namespace wal
{
    struct WalPosition
    {
        StreamId stream_id = 0;
        EpochId epoch = 0;
        SequenceNumber sequence = 0;

        [[nodiscard]] bool is_valid() const noexcept;
        [[nodiscard]] bool same_stream_epoch(const WalPosition& other) const noexcept;
    };
}