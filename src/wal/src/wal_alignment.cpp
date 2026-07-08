/**
 * @file wal_alignment.cpp
 * @brief Implements byte-alignment helpers for WAL records.
 */

#include "wal/wal_alignment.hpp"

#include <limits>

namespace wal
{
    std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) noexcept
    {
        if (alignment == 0) {
            return value;
        }

        const auto remainder = value % alignment;
        if (remainder == 0) {
            return value;
        }

        const std::uint32_t padding = alignment - remainder;
        if (value > std::numeric_limits<std::uint32_t>::max() - padding) {
            return value;
        }

        return value + padding;
    }

    std::uint32_t padding_for(std::uint32_t value, std::uint32_t alignment) noexcept
    {
        return align_up(value, alignment) - value;
    }
}
