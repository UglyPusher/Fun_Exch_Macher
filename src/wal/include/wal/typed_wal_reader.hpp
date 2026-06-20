#pragma once

/**
 * @file typed_wal_reader.hpp
 * @brief Converts validated raw WAL payloads into trivially-copyable records.
 *
 * The typed reader checks record type and payload size before copying bytes
 * into a DTO. It must not validate the business meaning of that DTO.
 */

#include "wal/raw_wal_reader.hpp"

#include <cstring>
#include <span>
#include <type_traits>

namespace wal
{
    /**
     * @brief Adapter from RawWalReader to one concrete record type.
     */
    template <typename TRecord, RecordType TRecordType>
    class TypedWalReader
    {
    public:
        /**
         * @brief Binds the adapter to an existing raw reader.
         */
        explicit TypedWalReader(RawWalReader& raw_reader)
            : raw_reader_(raw_reader)
        {
            static_assert(std::is_trivially_copyable_v<TRecord>);
        }

        /**
         * @brief Reads the next record after type and size checks.
         */
        WalReadResult read_next(TRecord& record)
        {
            WalRecordView view;
            auto result = raw_reader_.read_next(view);
            if (result.status != WalReadStatus::RecordRead) {
                return result;
            }

            if (view.header.record_type != TRecordType) {
                result.status = WalReadStatus::Failed;
                result.error = WalError::RecordTypeMismatch;
                return result;
            }

            if (view.payload.size() != sizeof(TRecord)) {
                result.status = WalReadStatus::Failed;
                result.error = WalError::PayloadSizeMismatch;
                return result;
            }

            std::memcpy(&record, view.payload.data(), sizeof(TRecord));
            return result;
        }

        /**
         * @brief Returns the last raw position accepted by the underlying reader.
         */
        [[nodiscard]] WalPosition last_position() const noexcept
        {
            return raw_reader_.last_position();
        }

    private:
        RawWalReader& raw_reader_;
    };
}
