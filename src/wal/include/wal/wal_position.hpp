#pragma once

/**
 * @file wal_position.hpp
 * @brief Stream-local WAL record position.
 *
 * A position identifies one record by stream, epoch, and sequence. It is not a
 * global ordering key across all streams.
 */

#include "wal/wal_types.hpp"

namespace wal
{
    /**
     * @brief Identity of a record inside one WAL stream epoch.
     */
    struct WalPosition
    {
        StreamId stream_id = 0;
        EpochId epoch = 0;
        SequenceNumber sequence = 0;

        /**
         * @brief Checks whether all identity fields are non-zero.
         */
        [[nodiscard]] bool is_valid() const noexcept;
        /**
         * @brief Checks whether another position belongs to the same stream epoch.
         */
        [[nodiscard]] bool same_stream_epoch(const WalPosition& other) const noexcept;
    };
}
