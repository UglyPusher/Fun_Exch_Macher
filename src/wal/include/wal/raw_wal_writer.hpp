#pragma once

/**
 * @file raw_wal_writer.hpp
 * @brief Payload-agnostic WAL append interface.
 *
 * Raw writers persist bytes with WAL headers and sequencing. They must not
 * inspect trading commands or execution event meaning.
 */

#include "wal/wal_result.hpp"
#include "wal/wal_types.hpp"

#include <cstddef>
#include <span>

namespace wal
{
    /**
     * @brief Appends raw payloads as WAL records.
     */
    class RawWalWriter
    {
    public:
        virtual ~RawWalWriter() = default;

        /**
         * @brief Appends one payload with the given WAL record type.
         */
        virtual WalRawAppendResult append(
            RecordType record_type,
            std::span<const std::byte> payload) = 0;

        /**
         * @brief Flushes appended bytes to the operating system.
         */
        virtual WalFlushResult flush_pending_writes() = 0;

        /**
         * @brief Returns the last appended WAL position.
         */
        [[nodiscard]] virtual WalPosition last_position() const noexcept = 0;
    };
}
