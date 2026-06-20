#pragma once

/**
 * @file typed_wal_writer.hpp
 * @brief Converts trivially-copyable records into raw WAL payloads.
 *
 * The typed writer preserves the DTO bytes and assigns a fixed record type. It
 * must not inspect business fields inside the record.
 */

#include "wal/raw_wal_writer.hpp"

#include <span>
#include <type_traits>

namespace wal
{
    /**
     * @brief Adapter from one concrete record type to RawWalWriter.
     */
    template <typename TRecord, RecordType TRecordType>
    class TypedWalWriter
    {
    public:
        /**
         * @brief Binds the adapter to an existing raw writer.
         */
        explicit TypedWalWriter(RawWalWriter& raw_writer)
            : raw_writer_(raw_writer)
        {
            static_assert(std::is_trivially_copyable_v<TRecord>);
        }

        /**
         * @brief Appends one typed record as a binary payload.
         */
        WalAppendResult append(const TRecord& record)
        {
            auto bytes = std::as_bytes(std::span{&record, 1});
            return raw_writer_.append(TRecordType, bytes);
        }

        /**
         * @brief Commits through the underlying raw writer.
         */
        WalCommitResult commit()
        {
            return raw_writer_.commit();
        }

        /**
         * @brief Returns the last appended raw position.
         */
        [[nodiscard]] WalPosition last_position() const noexcept
        {
            return raw_writer_.last_position();
        }

    private:
        RawWalWriter& raw_writer_;
    };
}
