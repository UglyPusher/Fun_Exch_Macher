#pragma once

#include "wal/wal_result.hpp"
#include "wal/wal_types.hpp"

#include <cstddef>
#include <span>

namespace wal
{
    class RawWalWriter
    {
    public:
        virtual ~RawWalWriter() = default;

        virtual WalAppendResult append(
            RecordType record_type,
            std::span<const std::byte> payload) = 0;

        virtual WalCommitResult commit() = 0;

        [[nodiscard]] virtual WalPosition last_position() const noexcept = 0;
    };
}