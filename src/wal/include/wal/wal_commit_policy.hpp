#pragma once

/**
 * @file wal_commit_policy.hpp
 * @brief Commit policies for publishing pending WAL writes.
 *
 * Commit policies decide how a writer makes appended records visible. They must
 * not alter record bytes, sequence numbers, or domain payload semantics.
 */

#include "wal/raw_wal_writer.hpp"

namespace wal
{
    /**
     * @brief Strategy interface for committing a raw WAL writer.
     */
    class WalCommitPolicy
    {
    public:
        virtual ~WalCommitPolicy() = default;

        /**
         * @brief Commits pending records using the strategy implementation.
         */
        virtual WalCommitResult commit(RawWalWriter& writer) = 0;
    };

    /**
     * @brief Commit policy that delegates to the writer flush/commit path.
     */
    class FlushCommitPolicy final : public WalCommitPolicy
    {
    public:
        WalCommitResult commit(RawWalWriter& writer) override;
    };

    /**
     * @brief Prototype policy that commits through the writer without extra sync behavior.
     */
    class NoSyncCommitPolicy final : public WalCommitPolicy
    {
    public:
        WalCommitResult commit(RawWalWriter& writer) override;
    };
}
