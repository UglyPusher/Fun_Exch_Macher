#include "wal/wal_commit_policy.hpp"
#include "wal/raw_wal_writer.hpp"

namespace wal
{
    WalCommitResult FlushCommitPolicy::commit(RawWalWriter& writer)
    {
        return writer.commit();
    }

    WalCommitResult NoSyncCommitPolicy::commit(RawWalWriter& writer)
    {
        return {
            .status = WalCommitStatus::Committed,
            .error = WalError::None,
            .committed_up_to = writer.last_position()
        };
    }
}