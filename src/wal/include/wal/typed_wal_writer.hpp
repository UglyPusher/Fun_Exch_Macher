#pragma once

/**
 * @file typed_wal_writer.hpp
 * @brief Converts trivially-copyable records into WAL payloads.
 *
 * The typed writer preserves the DTO bytes and assigns a fixed record type. It
 * must not inspect business fields inside the record.
 */

#include "wal/wal.hpp"

#include <span>
#include <type_traits>

namespace wal
{
    /**
     * @brief Adapter from one concrete record type to Wal.
     */
    template <typename TRecord, RecordType TRecordType>
    class TypedWalWriter
    {
    public:
        /**
         * @brief Binds the adapter to an existing WAL facade.
         */
        explicit TypedWalWriter(Wal& wal)
            : wal_(wal)
        {
            static_assert(std::is_trivially_copyable_v<TRecord>);
        }

        /**
         * @brief Appends one typed record as a binary payload.
         */
        WalAppendResult append(const TRecord& record)
        {
            const auto bytes = std::as_bytes(std::span{&record, 1});
            const WalAppendResult result = wal_.append(WalMessageView{
                .record_type = TRecordType,
                .payload = bytes
            });
            if (result.ok()) {
                last_position_ = result.position;
            }
            return result;
        }

        /**
         * @brief Returns the last appended raw position.
         */
        [[nodiscard]] WalPosition last_position() const noexcept
        {
            return last_position_;
        }

    private:
        Wal& wal_;
        WalPosition last_position_{};
    };
}
