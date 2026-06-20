#pragma once

/**
 * @file wal_result.hpp
 * @brief Result types returned by WAL append, commit, and read operations.
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
    enum class WalAppendStatus
    {
        Appended,
        Rejected,
        Failed
    };

    /**
     * @brief Result of appending one record to a WAL writer.
     */
    struct WalAppendResult
    {
        WalAppendStatus status = WalAppendStatus::Failed;
        WalError error = WalError::None;
        WalPosition position {};
    };

    /**
     * @brief Status of publishing appended records.
     */
    enum class WalCommitStatus
    {
        Committed,
        Failed
    };

    /**
     * @brief Result of committing pending WAL records.
     */
    struct WalCommitResult
    {
        WalCommitStatus status = WalCommitStatus::Failed;
        WalError error = WalError::None;
        WalPosition committed_up_to {};
    };

    /**
     * @brief Status of reading one record.
     */
    enum class WalReadStatus
    {
        RecordRead,
        EndOfLog,
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
    };
}
