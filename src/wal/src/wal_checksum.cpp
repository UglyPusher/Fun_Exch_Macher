#include "wal/wal_checksum.hpp"

namespace wal {

std::uint32_t calculate_crc32(std::span<const std::byte> bytes) noexcept
{
    std::uint32_t checksum = 0;
    for (const auto byte : bytes) {
        checksum = checksum * 31U + static_cast<std::uint32_t>(byte);
    }
    return checksum;
}

} // namespace wal
