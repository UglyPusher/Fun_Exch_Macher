#pragma once

/**
 * @file wal_alignment.hpp
 * @brief Alignment helpers for binary WAL record layout.
 *
 * WAL records are padded to a fixed boundary so readers can move from one
 * validated record to the next without domain-specific parsing. This is an
 * internal WAL-v0 implementation detail; normal users should include wal/wal.hpp.
 */

#include <cstdint>

namespace wal
{
    /**
     * @brief Rounds a byte count up to the requested alignment.
     */
    [[nodiscard]] std::uint32_t align_up(
        std::uint32_t value,
        std::uint32_t alignment) noexcept;

    /**
     * @brief Returns how many padding bytes are required for the alignment.
     */
    [[nodiscard]] std::uint32_t padding_for(
        std::uint32_t value,
        std::uint32_t alignment) noexcept;
}
