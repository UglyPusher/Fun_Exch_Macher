#pragma once

#include <cstdint>

namespace wal
{
    using StreamId = std::uint64_t;
    using EpochId = std::uint64_t;
    using SequenceNumber = std::uint64_t;
    using RecordType = std::uint32_t;
    using ByteSize = std::uint32_t;

    constexpr std::uint32_t WalRecordMagic = 0x57414C52;   // "WALR"
    constexpr std::uint32_t WalSegmentMagic = 0x57414C53;  // "WALS"

    constexpr std::uint16_t WalFormatVersion = 1;
    constexpr std::uint32_t DefaultRecordAlignment = 8;
}
