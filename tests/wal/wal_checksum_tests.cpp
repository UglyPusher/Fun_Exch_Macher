#include "wal/wal_checksum.hpp"

#include <array>
#include <cstddef>

int main()
{
    const std::array first{std::byte{1}, std::byte{2}, std::byte{3}};
    const std::array same{std::byte{1}, std::byte{2}, std::byte{3}};
    const std::array changed{std::byte{1}, std::byte{2}, std::byte{4}};

    const auto checksum = wal::calculate_crc32(first);
    if (checksum == 0) {
        return 1;
    }

    if (checksum != wal::calculate_crc32(same)) {
        return 2;
    }

    if (checksum == wal::calculate_crc32(changed)) {
        return 3;
    }

    return 0;
}
