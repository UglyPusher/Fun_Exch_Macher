/**
 * @file wal_record_header.cpp
 * @brief Implements WAL record header validation and CRC calculation.
 */

#include "wal/wal_record_header.hpp"

#include "wal/wal_alignment.hpp"
#include "wal/wal_checksum.hpp"

namespace wal
{
    bool WalRecordHeader::has_valid_static_fields() const noexcept
    {
        const auto expected_record_length = align_up(
            static_cast<std::uint32_t>(sizeof(WalRecordHeader)) + payload_length,
            DefaultRecordAlignment);

        return magic == WalRecordMagic
            && version == WalFormatVersion
            && header_size == sizeof(WalRecordHeader)
            && record_length >= sizeof(WalRecordHeader)
            && record_length % DefaultRecordAlignment == 0
            && record_length == expected_record_length;
    }

    std::uint32_t calculate_record_header_crc(WalRecordHeader header) noexcept
    {
        header.header_crc = 0;
        const auto* data = reinterpret_cast<const std::byte*>(&header);
        return calculate_crc32({data, sizeof(header)});
    }
}
