#pragma once

#include <cstdint>
#include <type_traits>

namespace domain
{
    struct ExecutionEventRecordV1
    {
        std::uint64_t event_sequence = 0;
        std::uint64_t command_sequence = 0;

        std::uint64_t source_ingress_epoch = 0;
        std::uint64_t source_ingress_sequence = 0;

        std::uint64_t order_id = 0;
        std::uint64_t contra_order_id = 0;
        std::uint64_t trade_id = 0;

        std::int64_t price_ticks = 0;
        std::int64_t quantity_lots = 0;
        std::int64_t remaining_quantity_lots = 0;

        std::uint32_t instrument_id = 0;
        std::uint16_t event_type = 0;
        std::uint16_t side = 0;
        std::uint16_t rejection_reason = 0;
        std::uint16_t reserved = 0;
    };

    static_assert(std::is_trivially_copyable_v<ExecutionEventRecordV1>);
}