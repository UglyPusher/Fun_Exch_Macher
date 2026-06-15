#include "wal/wal_segment_header.hpp"

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
}
