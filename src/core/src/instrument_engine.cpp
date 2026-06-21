/**
 * @file instrument_engine.cpp
 * @brief Implements command dispatch for the per-instrument matching state.
 */

#include "core/instrument_engine.hpp"

#include <optional>

namespace core
{
    InstrumentEngine::InstrumentEngine(std::uint64_t first_event_sequence) noexcept
        : order_book_(first_event_sequence)
    {
    }

    std::vector<domain::ExecutionEventRecordV1> InstrumentEngine::apply(
        const domain::OrderCommandRecordV1& command)
    {
        const std::optional<CommandType> command_type = decode_command_type(command.command_type);
        if (!command_type.has_value()) {
            return order_book_.reject_unsupported_command(command);
        }

        if (*command_type == CommandType::NewOrder) {
            return order_book_.apply_new_order(command);
        }
        if (*command_type == CommandType::CancelOrder) {
            return order_book_.apply_cancel_order(command);
        }
        if (*command_type == CommandType::ReplaceOrder) {
            return order_book_.apply_replace_order(command);
        }

        return order_book_.reject_unsupported_command(command);
    }

    const OrderBook& InstrumentEngine::order_book() const noexcept
    {
        return order_book_;
    }
}
