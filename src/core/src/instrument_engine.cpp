#include "core/instrument_engine.hpp"

namespace core
{
    InstrumentEngine::InstrumentEngine(std::uint64_t first_event_sequence) noexcept
        : order_book_(first_event_sequence)
    {
    }

    std::vector<domain::ExecutionEventRecordV1> InstrumentEngine::apply(
        const domain::OrderCommandRecordV1& command)
    {
        return order_book_.apply_new_order(command);
    }

    const OrderBook& InstrumentEngine::order_book() const noexcept
    {
        return order_book_;
    }
}
