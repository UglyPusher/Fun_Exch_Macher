/**
 * @file dummy_instrument_engine.cpp
 * @brief Implements the historical scaffold engine used by simple tests.
 */

#include "core/dummy_instrument_engine.hpp"

namespace core
{
    DummyInstrumentEngine::DummyInstrumentEngine(std::uint64_t first_event_sequence) noexcept
        : next_event_sequence_(first_event_sequence)
    {
    }

    domain::ExecutionEventRecordV1 DummyInstrumentEngine::apply(
        const domain::OrderCommandRecordV1& command) noexcept
    {
        domain::ExecutionEventRecordV1 event{};
        event.event_sequence = next_event_sequence_++;
        event.command_sequence = command.command_sequence;
        event.source_ingress_epoch = command.source_ingress_epoch;
        event.source_ingress_sequence = command.source_ingress_sequence;
        event.order_id = command.order_id;
        event.contra_order_id = 0;
        event.trade_id = 0;
        event.price_ticks = command.price_ticks;
        event.quantity_lots = command.quantity_lots;
        event.remaining_quantity_lots = command.quantity_lots;
        event.instrument_id = command.instrument_id;
        event.event_type = static_cast<std::uint16_t>(ExecutionEventType::OrderAccepted);
        event.side = command.side;
        event.rejection_reason = 0;
        event.reserved = 0;
        return event;
    }
}
