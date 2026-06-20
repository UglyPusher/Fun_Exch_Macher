#pragma once

/**
 * @file wal_checksum.hpp
 * @brief CRC32 helper used by WAL headers and payload validation.
 *
 * Checksums are physical integrity checks. They do not prove that payload bytes
 * are semantically valid domain records.
 */

#include <cstddef>
#include <cstdint>
#include <span>

namespace wal
{
    /**
     * @brief Calculates CRC32 for a contiguous byte span.
     */
    std::uint32_t calculate_crc32(std::span<const std::byte> bytes) noexcept;
}
