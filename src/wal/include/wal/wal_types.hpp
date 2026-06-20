#pragma once

/**
 * @file wal_types.hpp
 * @brief Primitive WAL ids and binary format constants.
 *
 * These values describe physical WAL record identity and layout. They must not
 * depend on domain command or event semantics.
 */

#include <cstdint>

namespace wal
{
    /**
     * @brief Logical stream identifier local to the WAL layer.
     */
    using StreamId = std::uint64_t;
    /**
     * @brief Stream epoch identifier used to separate sequence ranges.
     */
    using EpochId = std::uint64_t;
    /**
     * @brief Monotonic record sequence inside one stream epoch.
     */
    using SequenceNumber = std::uint64_t;
    /**
     * @brief Numeric payload classifier stored in each WAL record header.
     */
    using RecordType = std::uint32_t;

    constexpr std::uint32_t WalRecordMagic = 0x57414C52;   // "WALR"
    constexpr std::uint32_t WalSegmentMagic = 0x57414C53;  // "WALS"

    constexpr std::uint16_t WalFormatVersion = 1;
    constexpr std::uint32_t DefaultRecordAlignment = 8;
}
