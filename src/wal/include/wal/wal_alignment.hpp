#pragma once

#include <cstdint>

namespace wal
{
    [[nodiscard]] std::uint32_t align_up(
        std::uint32_t value,
        std::uint32_t alignment) noexcept;

    [[nodiscard]] std::uint32_t padding_for(
        std::uint32_t value,
        std::uint32_t alignment) noexcept;
}
