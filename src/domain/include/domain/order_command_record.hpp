#pragma once

#include <cstdint>
#include <type_traits>

namespace domain
{
    struct OrderCommandRecordV1
    {
        std::uint64_t command_sequence = 0;
        std::uint64_t source_ingress_epoch = 0;
        std::uint64_t source_ingress_sequence = 0;

        std::uint64_t order_id = 0;
        std::uint64_t replacement_order_id = 0;
        std::uint64_t client_id = 0;

        std::int64_t price_ticks = 0;
        std::int64_t quantity_lots = 0;

        std::uint32_t instrument_id = 0;
        std::uint16_t command_type = 0;
        std::uint16_t side = 0;
        std::uint16_t time_in_force = 0;
        std::uint16_t reserved = 0;
    };

    static_assert(std::is_trivially_copyable_v<OrderCommandRecordV1>);
}
