#include "core/instrument_engine.hpp"

// Implements command-type dispatch for the per-instrument matching state machine.

#include <optional>

namespace core
{
    namespace
    {
        std::optional<CommandType> decode_command_type(const domain::OrderCommandRecordV1& command) noexcept
        {
            switch (static_cast<CommandType>(command.command_type)) {
            case CommandType::NewOrder:
            case CommandType::CancelOrder:
            case CommandType::ReplaceOrder:
                return static_cast<CommandType>(command.command_type);
            default:
                return std::nullopt;
            }
        }
    }

    InstrumentEngine::InstrumentEngine(std::uint64_t first_event_sequence) noexcept
        : order_book_(first_event_sequence)
    {
    }

    std::vector<domain::ExecutionEventRecordV1> InstrumentEngine::apply(
        const domain::OrderCommandRecordV1& command)
    {
        const std::optional<CommandType> command_type = decode_command_type(command);
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
