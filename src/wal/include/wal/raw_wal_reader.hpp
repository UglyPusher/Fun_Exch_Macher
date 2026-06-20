#pragma once

/**
 * @file raw_wal_reader.hpp
 * @brief Payload-agnostic WAL read interface.
 *
 * Raw readers validate physical record integrity before exposing payload bytes.
 * They must not know domain DTO semantics.
 */

#include "wal/wal_record_view.hpp"
#include "wal/wal_result.hpp"

namespace wal
{
    /**
     * @brief Reads validated raw WAL records from one stream.
     */
    class RawWalReader
    {
    public:
        virtual ~RawWalReader() = default;

        /**
         * @brief Reads the next validated record view or reports end/failure.
         */
        virtual WalReadResult read_next(WalRecordView& out) = 0;

        /**
         * @brief Returns the last successfully read WAL position.
         */
        [[nodiscard]] virtual WalPosition last_position() const noexcept = 0;
    };
}
