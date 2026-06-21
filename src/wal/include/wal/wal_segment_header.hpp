#pragma once

/**
 * @file wal_segment_header.hpp
 * @brief Fixed binary header stored at the beginning of a WAL segment.
 *
 * The segment header identifies the stream epoch and first sequence in a file.
 * It must stay independent from the payload types stored after it. This is an
 * internal WAL-v0 implementation detail; normal users should include wal/wal.hpp.
 */

#include "wal/wal_types.hpp"

#include <cstdint>
#include <type_traits>

namespace wal
{
    /**
     * @brief On-disk metadata for one WAL segment file.
     */
    struct WalSegmentHeader
    {
        std::uint32_t magic = WalSegmentMagic;
        std::uint16_t version = WalFormatVersion;
        std::uint16_t header_size = sizeof(WalSegmentHeader);

        StreamId stream_id = 0;
        EpochId epoch = 0;
        SequenceNumber first_sequence = 1;

        std::uint32_t flags = 0;
        std::uint32_t header_crc = 0;

        /**
         * @brief Checks magic, version, and header size fields.
         */
        [[nodiscard]] bool has_valid_static_fields() const noexcept;
    };

    static_assert(std::is_trivially_copyable_v<WalSegmentHeader>);
    static_assert(std::is_standard_layout_v<WalSegmentHeader>);

    /**
     * @brief Calculates the CRC for a segment header with header_crc ignored.
     */
    [[nodiscard]] std::uint32_t calculate_segment_header_crc(WalSegmentHeader header) noexcept;
}
