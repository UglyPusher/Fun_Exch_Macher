#pragma once

#include "wal/raw_wal_writer.hpp"

namespace wal
{
    class WalCommitPolicy
    {
    public:
        virtual ~WalCommitPolicy() = default;

        virtual WalCommitResult commit(RawWalWriter& writer) = 0;
    };

    class FlushCommitPolicy final : public WalCommitPolicy
    {
    public:
        WalCommitResult commit(RawWalWriter& writer) override;
    };

    class NoSyncCommitPolicy final : public WalCommitPolicy
    {
    public:
        WalCommitResult commit(RawWalWriter& writer) override;
    };
}
