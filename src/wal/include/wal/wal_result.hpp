#pragma once

/**
 * @file wal_result.hpp
 * @brief Result types returned by WAL append, flush, and read operations.
 *
 * WAL APIs return explicit statuses so callers can distinguish rejected input,
 * physical failures, end of log, and successful operations.
 */

#include "wal/wal_error.hpp"
#include "wal/wal_position.hpp"

namespace wal
{
    /**
     * @brief Status of appending one record.
     */
    enum class WalRawAppendStatus
    {
        Appended,
        Rejected,
        Failed
    };

    /**
     * @brief Result of appending one record to a WAL writer.
     */
    struct WalRawAppendResult
    {
        WalRawAppendStatus status = WalRawAppendStatus::Failed;
        WalError error = WalError::None;
        WalPosition position {};
    };

    /**
     * @brief Status of flushing appended bytes to the operating system.
     */
    enum class WalFlushStatus
    {
        Flushed,
        Failed
    };

    /**
     * @brief Result of flushing pending WAL bytes.
     */
    struct WalFlushResult
    {
        WalFlushStatus status = WalFlushStatus::Failed;
        WalError error = WalError::None;
        WalPosition flushed_up_to {};
    };

    /**
     * @brief Status of reading one record.
     */
    enum class WalReadStatus
    {
        RecordRead,
        EndOfLog,
        InvalidArgument,
        Corrupted,
        Failed
    };

    /**
     * @brief Result of reading one WAL record.
     */
    struct WalReadResult
    {
        WalReadStatus status = WalReadStatus::Failed;
        WalError error = WalError::None;
        WalPosition position {};

        /**
         * @brief Checks whether one committed record was read.
         */
        [[nodiscard]] bool ok() const noexcept;
    };
}
