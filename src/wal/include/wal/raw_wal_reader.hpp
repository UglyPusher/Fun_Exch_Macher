#pragma once

#include "wal/wal_record_view.hpp"
#include "wal/wal_result.hpp"

namespace wal
{
    class RawWalReader
    {
    public:
        virtual ~RawWalReader() = default;

        virtual WalReadResult read_next(WalRecordView& out) = 0;

        [[nodiscard]] virtual WalPosition last_position() const noexcept = 0;
    };
}