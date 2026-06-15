#pragma once

#include "wal/raw_wal_writer.hpp"

#include <span>
#include <type_traits>

namespace wal
{
    template <typename TRecord, RecordType TRecordType>
    class TypedWalWriter
    {
    public:
        explicit TypedWalWriter(RawWalWriter& raw_writer)
            : raw_writer_(raw_writer)
        {
            static_assert(std::is_trivially_copyable_v<TRecord>);
        }

        WalAppendResult append(const TRecord& record)
        {
            auto bytes = std::as_bytes(std::span{&record, 1});
            return raw_writer_.append(TRecordType, bytes);
        }

        WalCommitResult commit()
        {
            return raw_writer_.commit();
        }

        [[nodiscard]] WalPosition last_position() const noexcept
        {
            return raw_writer_.last_position();
        }

    private:
        RawWalWriter& raw_writer_;
    };
}