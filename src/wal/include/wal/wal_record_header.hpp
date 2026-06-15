#pragma once

#include "wal/wal_types.hpp"

#include <cstdint>
#include <type_traits>

namespace wal
{
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

        [[nodiscard]] bool has_valid_static_fields() const noexcept;
    };

    static_assert(std::is_trivially_copyable_v<WalRecordHeader>);
    static_assert(std::is_standard_layout_v<WalRecordHeader>);

    [[nodiscard]] std::uint32_t calculate_record_header_crc(WalRecordHeader header) noexcept;
}
