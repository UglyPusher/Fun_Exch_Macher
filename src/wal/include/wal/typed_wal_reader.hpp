#pragma once

/**
 * @file typed_wal_reader.hpp
 * @brief Converts WAL payloads into trivially-copyable records.
 *
 * The typed reader checks record type and payload size before copying bytes
 * into a DTO. It must not validate the business meaning of that DTO.
 */

#include "wal/wal.hpp"

#include <cstring>
#include <span>
#include <type_traits>

namespace wal
{
    /**
     * @brief Adapter from Wal to one concrete record type.
     */
    template <typename TRecord, RecordType TRecordType>
    class TypedWalReader
    {
    public:
        /**
         * @brief Binds the adapter to an existing WAL facade and caller-owned cursor.
         */
        TypedWalReader(Wal& wal, WalCursor& cursor)
            : wal_(wal), cursor_(cursor)
        {
            static_assert(std::is_trivially_copyable_v<TRecord>);
        }

        /**
         * @brief Reads the next record after type and size checks.
         */
        WalReadResult read_next(TRecord& record)
        {
            WalRecord wal_record;
            auto result = wal_.read_next(cursor_, wal_record);
            if (result.status != WalReadStatus::RecordRead) {
                return result;
            }

            if (wal_record.record_type != TRecordType) {
                result.status = WalReadStatus::Failed;
                result.error = WalError::RecordTypeMismatch;
                return result;
            }

            if (wal_record.payload.size() != sizeof(TRecord)) {
                result.status = WalReadStatus::Failed;
                result.error = WalError::PayloadSizeMismatch;
                return result;
            }

            std::memcpy(&record, wal_record.payload.data(), sizeof(TRecord));
            last_position_ = result.position;
            return result;
        }

        /**
         * @brief Returns the last raw position accepted by the underlying reader.
         */
        [[nodiscard]] WalPosition last_position() const noexcept
        {
            return last_position_;
        }

    private:
        Wal& wal_;
        WalCursor& cursor_;
        WalPosition last_position_{};
    };
}
