#pragma once

#include "wal/wal_types.hpp"

#include <cstdint>
#include <type_traits>

namespace wal
{
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

        [[nodiscard]] bool has_valid_static_fields() const noexcept;
    };

    static_assert(std::is_trivially_copyable_v<WalSegmentHeader>);
}