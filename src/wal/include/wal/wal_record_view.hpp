#pragma once

/**
 * @file wal_record_view.hpp
 * @brief Non-owning view of one validated raw WAL record.
 *
 * Readers expose this type only after header, checksum, and sequence validation
 * succeed. The payload still has no domain meaning at this layer.
 */

#include "wal/wal_record_header.hpp"

#include <cstddef>
#include <span>

namespace wal
{
    /**
     * @brief Validated record header plus payload bytes.
     */
    struct WalRecordView
    {
        WalRecordHeader header {};
        std::span<const std::byte> payload {};
    };
}
