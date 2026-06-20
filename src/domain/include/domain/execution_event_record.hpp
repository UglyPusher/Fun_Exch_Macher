#pragma once

/**
 * @file execution_event_record.hpp
 * @brief Durable execution event DTO emitted by the matching core.
 *
 * ExecutionEventRecordV1 is the event payload written to the Execution Event
 * WAL and consumed by replay/projection code. This file must not contain
 * matching rules, projection aggregation, or WAL physical-format code.
 */

#include <cstdint>
#include <type_traits>

namespace domain
{
    /**
     * @brief Versioned fact emitted by deterministic order matching.
     *
     * The type is trivially copyable because typed WAL adapters persist it as a
     * binary payload. Numeric event and side fields are durable wire values and
     * must be interpreted by consumers at their module boundary.
     */
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
