#include "wal/wal_alignment.hpp"

namespace wal {

ByteSize align_to_wal_boundary(ByteSize size) noexcept
{
    const auto remainder = size % kWalAlignment;
    return remainder == 0 ? size : size + (kWalAlignment - remainder);
}

} // namespace wal
