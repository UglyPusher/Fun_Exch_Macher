#pragma once

#include "wal/wal_record_header.hpp"

#include <cstddef>
#include <span>

namespace wal
{
    struct WalRecordView
    {
        WalRecordHeader header {};
        std::span<const std::byte> payload {};
    };
}