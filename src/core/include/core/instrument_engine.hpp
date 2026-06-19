#pragma once

// InstrumentEngine routes normalized order commands to one deterministic OrderBook.
// Owns: the per-instrument OrderBook instance and command-type dispatch.
// Does not own: WAL I/O, scenario parsing, projection, or external services.
// Invariant: every command is applied to the same underlying OrderBook sequence.

#include "core/order_book.hpp"
#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"

#include <cstdint>
#include <vector>

namespace core
{
    class InstrumentEngine
    {
    public:
        explicit InstrumentEngine(std::uint64_t first_event_sequence = 1) noexcept;

        [[nodiscard]] std::vector<domain::ExecutionEventRecordV1> apply(
            const domain::OrderCommandRecordV1& command);

        [[nodiscard]] const OrderBook& order_book() const noexcept;

    private:
        OrderBook order_book_;
    };
}
