#include "wal/wal_file.hpp"

namespace wal {

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) noexcept
{
    const auto remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

std::uint32_t padding_for(std::uint32_t value, std::uint32_t alignment) noexcept
{
    return align_up(value, alignment) - value;
}

} // namespace wal
