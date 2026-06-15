#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace wal
{
    std::uint32_t calculate_crc32(std::span<const std::byte> bytes) noexcept;
}