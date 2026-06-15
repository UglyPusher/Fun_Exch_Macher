#include "wal/wal_record_header.hpp"

namespace wal
{
    bool WalRecordHeader::has_valid_static_fields() const noexcept
    {
        return magic == WalRecordMagic
            && version == WalFormatVersion
            && header_size == sizeof(WalRecordHeader)
            && record_length >= sizeof(WalRecordHeader)
            && payload_length <= record_length - sizeof(WalRecordHeader);
    }
}
