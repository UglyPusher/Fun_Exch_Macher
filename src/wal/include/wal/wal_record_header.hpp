#pragma once

/**
 * @file wal_record_header.hpp
 * @brief Fixed binary header stored before every WAL payload.
 *
 * The record header owns physical metadata: record type, stream identity,
 * sequence, payload length, and checksums. It must not encode domain-level
 * validity. This is an internal WAL-v0 implementation detail; normal users
 * should include wal/wal.hpp.
 */

#include "wal/wal_types.hpp"

#include <cstdint>
#include <type_traits>

namespace wal
{
    /**
     * @brief On-disk metadata for one WAL record.
     */
    struct WalRecordHeader
    {
        std::uint32_t magic = WalRecordMagic;
        std::uint16_t version = WalFormatVersion;
        std::uint16_t header_size = sizeof(WalRecordHeader);

        std::uint32_t record_length = 0;
        RecordType record_type = 0;

        StreamId stream_id = 0;
        EpochId epoch = 0;
        SequenceNumber sequence = 0;

        std::uint32_t payload_length = 0;
        std::uint32_t payload_crc = 0;

        std::uint32_t header_crc = 0;

        /**
         * @brief Checks magic, version, and header size fields that do not need payload data.
         */
        [[nodiscard]] bool has_valid_static_fields() const noexcept;
    };

    static_assert(std::is_trivially_copyable_v<WalRecordHeader>);
    static_assert(std::is_standard_layout_v<WalRecordHeader>);

    /**
     * @brief Calculates the CRC for a record header with header_crc ignored.
     */
    [[nodiscard]] std::uint32_t calculate_record_header_crc(WalRecordHeader header) noexcept;
}
