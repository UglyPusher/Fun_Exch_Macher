#pragma once

#include "wal/wal_error.hpp"
#include "wal/wal_position.hpp"

namespace wal
{
    enum class WalAppendStatus
    {
        Appended,
        Rejected,
        Failed
    };

    struct WalAppendResult
    {
        WalAppendStatus status = WalAppendStatus::Failed;
        WalError error = WalError::None;
        WalPosition position {};
    };

    enum class WalCommitStatus
    {
        Committed,
        Failed
    };

    struct WalCommitResult
    {
        WalCommitStatus status = WalCommitStatus::Failed;
        WalError error = WalError::None;
        WalPosition committed_up_to {};
    };

    enum class WalReadStatus
    {
        RecordRead,
        EndOfLog,
        Failed
    };

    struct WalReadResult
    {
        WalReadStatus status = WalReadStatus::Failed;
        WalError error = WalError::None;
        WalPosition position {};
    };
}