#pragma once

/**
 * @file instrument_engine.hpp
 * @brief Command dispatcher for one deterministic order book.
 *
 * InstrumentEngine owns command-type routing and one OrderBook instance. It
 * must not perform WAL I/O, scenario parsing, projection, or external service
 * calls.
 */

#include "core/order_book.hpp"
#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"

#include <cstdint>
#include <vector>

namespace core
{
    /**
     * @brief Applies normalized commands to one instrument book.
     *
     * The engine keeps all commands on the same OrderBook sequence. Unknown
     * command types are rejected explicitly instead of falling through to a
     * valid command path.
     */
    class InstrumentEngine
    {
    public:
        /**
         * @brief Creates an engine whose first emitted event uses the given sequence.
         */
        explicit InstrumentEngine(std::uint64_t first_event_sequence = 1) noexcept;

        /**
         * @brief Routes one normalized command to the matching operation it represents.
         */
        [[nodiscard]] std::vector<domain::ExecutionEventRecordV1> apply(
            const domain::OrderCommandRecordV1& command);

        /**
         * @brief Exposes the current book for replay diagnostics and tests.
         */
        [[nodiscard]] const OrderBook& order_book() const noexcept;

    private:
        OrderBook order_book_;
    };
}
