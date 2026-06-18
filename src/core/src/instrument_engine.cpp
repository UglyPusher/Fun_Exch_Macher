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
        if (command.command_type == static_cast<std::uint16_t>(CommandType::CancelOrder)) {
            return order_book_.apply_cancel_order(command);
        }
        if (command.command_type == static_cast<std::uint16_t>(CommandType::ReplaceOrder)) {
            return order_book_.apply_replace_order(command);
        }

        return order_book_.apply_new_order(command);
    }

    const OrderBook& InstrumentEngine::order_book() const noexcept
    {
        return order_book_;
    }
}
