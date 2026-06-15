#include "wal/wal_segment_header.hpp"

#include "wal/wal_checksum.hpp"

namespace wal
{
    bool WalSegmentHeader::has_valid_static_fields() const noexcept
    {
        return magic == WalSegmentMagic
            && version == WalFormatVersion
            && header_size == sizeof(WalSegmentHeader)
            && stream_id != 0
            && epoch != 0
            && first_sequence != 0;
    }

    std::uint32_t calculate_segment_header_crc(WalSegmentHeader header) noexcept
    {
        header.header_crc = 0;
        const auto* data = reinterpret_cast<const std::byte*>(&header);
        return calculate_crc32({data, sizeof(header)});
    }
}
