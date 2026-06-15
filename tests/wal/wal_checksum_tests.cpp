#include "wal/wal_checksum.hpp"

#include <array>
#include <cstddef>

int main()
{
    const std::array bytes{std::byte{1}, std::byte{2}, std::byte{3}};
    return wal::calculate_crc32(bytes) == 0 ? 1 : 0;
}
