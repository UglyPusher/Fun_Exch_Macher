#include "wal/wal_record_header.hpp"

namespace wal {

ByteSize wal_record_header_size() noexcept
{
    return sizeof(WalRecordHeader);
}

} // namespace wal
