#pragma once

/**
 * @file dummy_instrument_engine.hpp
 * @brief Earlier scaffold engine retained for simple command-to-event tests.
 *
 * DummyInstrumentEngine does not implement real matching. Do not extend it with
 * production rules; use InstrumentEngine and OrderBook for active matching.
 */

#include "core/matching_types.hpp"
#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"

#include <cstdint>

namespace core
{
    /**
     * @brief Minimal scaffold that emits one event per command.
     */
    class DummyInstrumentEngine
    {
    public:
        /**
         * @brief Creates a dummy engine with the next event sequence initialized.
         */
        explicit DummyInstrumentEngine(std::uint64_t first_event_sequence = 1) noexcept;

        /**
         * @brief Converts one command to a deterministic dummy execution event.
         */
        [[nodiscard]] domain::ExecutionEventRecordV1 apply(
            const domain::OrderCommandRecordV1& command) noexcept;

    private:
        std::uint64_t next_event_sequence_ = 1;
    };
}
