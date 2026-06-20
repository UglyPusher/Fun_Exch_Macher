/**
 * @file wal_checksum.cpp
 * @brief Implements CRC32 calculation used by WAL integrity checks.
 */

#include "wal/wal_checksum.hpp"

namespace wal
{
    std::uint32_t calculate_crc32(std::span<const std::byte> bytes) noexcept
    {
        std::uint32_t crc = 0xFFFFFFFFU;

        for (const auto byte : bytes) {
            crc ^= static_cast<std::uint32_t>(byte);
            for (int bit = 0; bit < 8; ++bit) {
                const auto mask = 0U - (crc & 1U);
                crc = (crc >> 1U) ^ (0xEDB88320U & mask);
            }
        }

        return ~crc;
    }
}
