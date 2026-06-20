#pragma once

/**
 * @file order_command_record.hpp
 * @brief Durable command DTO accepted by the matching core.
 *
 * OrderCommandRecordV1 is the normalized command payload written to the Command
 * WAL and replayed into the matcher. This file must not contain validation,
 * matching behavior, or WAL physical-format code.
 */

#include <cstdint>
#include <type_traits>

namespace domain
{
    /**
     * @brief Versioned normalized order command record.
     *
     * The type is trivially copyable because typed WAL adapters persist it as a
     * binary payload. Enum-like fields remain fixed-width integers at this
     * boundary and must be decoded by core logic before domain use.
     */
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
