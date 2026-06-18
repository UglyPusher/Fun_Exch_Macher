#include "event_dump.hpp"

#include "core/matching_types.hpp"

#include <ostream>

namespace app
{
    namespace
    {
        const char* command_type(std::uint16_t value)
        {
            switch (static_cast<core::CommandType>(value)) {
            case core::CommandType::NewOrder:
                return "NEW";
            case core::CommandType::CancelOrder:
                return "CANCEL";
            case core::CommandType::ReplaceOrder:
                return "REPLACE";
            }
            return "UNKNOWN";
        }

        const char* event_type(std::uint16_t value)
        {
            switch (static_cast<core::ExecutionEventType>(value)) {
            case core::ExecutionEventType::OrderAccepted:
                return "OrderAccepted";
            case core::ExecutionEventType::OrderRejected:
                return "OrderRejected";
            case core::ExecutionEventType::TradeExecuted:
                return "TradeExecuted";
            case core::ExecutionEventType::OrderRested:
                return "OrderRested";
            case core::ExecutionEventType::OrderPartiallyFilled:
                return "OrderPartiallyFilled";
            case core::ExecutionEventType::OrderFullyFilled:
                return "OrderFullyFilled";
            case core::ExecutionEventType::OrderCancelled:
                return "OrderCancelled";
            }
            return "UnknownEvent";
        }

        const char* side(std::uint16_t value)
        {
            switch (static_cast<core::Side>(value)) {
            case core::Side::Buy:
                return "BUY";
            case core::Side::Sell:
                return "SELL";
            }
            return "NA";
        }

        const char* rejection_reason(std::uint16_t value)
        {
            switch (static_cast<core::RejectionReason>(value)) {
            case core::RejectionReason::None:
                return "None";
            case core::RejectionReason::UnsupportedCommand:
                return "UnsupportedCommand";
            case core::RejectionReason::InvalidSide:
                return "InvalidSide";
            case core::RejectionReason::InvalidPrice:
                return "InvalidPrice";
            case core::RejectionReason::InvalidQuantity:
                return "InvalidQuantity";
            case core::RejectionReason::DuplicateOrderId:
                return "DuplicateOrderId";
            case core::RejectionReason::UnsupportedTimeInForce:
                return "UnsupportedTimeInForce";
            case core::RejectionReason::UnknownOrderId:
                return "UnknownOrderId";
            case core::RejectionReason::InstrumentMismatch:
                return "InstrumentMismatch";
            case core::RejectionReason::ReplaceWouldDuplicateOrderId:
                return "ReplaceWouldDuplicateOrderId";
            case core::RejectionReason::InvalidReplacementOrderId:
                return "InvalidReplacementOrderId";
            }
            return "UnknownReason";
        }
    }

    void print_command(std::ostream& out, const domain::OrderCommandRecordV1& command)
    {
        out << "cmd_seq=" << command.command_sequence
            << " type=" << command_type(command.command_type)
            << " instrument=" << command.instrument_id
            << " client=" << command.client_id
            << " order=" << command.order_id;
        if (command.replacement_order_id != 0) {
            out << " replacement=" << command.replacement_order_id;
        }
        if (command.side != 0) {
            out << " side=" << side(command.side);
        }
        if (command.price_ticks != 0 || command.quantity_lots != 0) {
            out << " price=" << command.price_ticks
                << " qty=" << command.quantity_lots;
        }
        out << '\n';
    }

    void print_event(std::ostream& out, const domain::ExecutionEventRecordV1& event)
    {
        out << "event_seq=" << event.event_sequence
            << " cmd_seq=" << event.command_sequence
            << " type=" << event_type(event.event_type)
            << " instrument=" << event.instrument_id
            << " order=" << event.order_id;
        if (event.contra_order_id != 0) {
            out << " contra=" << event.contra_order_id;
        }
        if (event.trade_id != 0) {
            out << " trade=" << event.trade_id;
        }
        out << " side=" << side(event.side)
            << " price=" << event.price_ticks
            << " qty=" << event.quantity_lots
            << " remaining=" << event.remaining_quantity_lots;
        if (event.rejection_reason != 0) {
            out << " reason=" << rejection_reason(event.rejection_reason);
        }
        out << '\n';
    }
}
