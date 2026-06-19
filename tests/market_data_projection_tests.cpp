#include "domain/matching_types.hpp"
#include "projections/market_data_projection.hpp"

#include <cstdint>
#include <vector>

namespace
{
    constexpr std::uint32_t test_instrument_id = 77;

    std::uint16_t encode(domain::ExecutionEventType event_type) noexcept
    {
        return static_cast<std::uint16_t>(event_type);
    }

    std::uint16_t encode(domain::Side side) noexcept
    {
        return static_cast<std::uint16_t>(side);
    }

    domain::ExecutionEventRecordV1 rested_event(
        std::uint64_t event_sequence,
        std::uint64_t order_id,
        domain::Side side,
        std::int64_t price_ticks,
        std::int64_t remaining_quantity_lots)
    {
        domain::ExecutionEventRecordV1 event{};
        event.event_sequence = event_sequence;
        event.order_id = order_id;
        event.price_ticks = price_ticks;
        event.remaining_quantity_lots = remaining_quantity_lots;
        event.instrument_id = test_instrument_id;
        event.event_type = encode(domain::ExecutionEventType::OrderRested);
        event.side = encode(side);
        return event;
    }

    domain::ExecutionEventRecordV1 accepted_event(std::uint64_t event_sequence, std::uint64_t order_id)
    {
        domain::ExecutionEventRecordV1 event{};
        event.event_sequence = event_sequence;
        event.order_id = order_id;
        event.instrument_id = test_instrument_id;
        event.event_type = encode(domain::ExecutionEventType::OrderAccepted);
        return event;
    }

    domain::ExecutionEventRecordV1 cancelled_event(std::uint64_t event_sequence, std::uint64_t order_id)
    {
        domain::ExecutionEventRecordV1 event{};
        event.event_sequence = event_sequence;
        event.order_id = order_id;
        event.instrument_id = test_instrument_id;
        event.event_type = encode(domain::ExecutionEventType::OrderCancelled);
        return event;
    }

    domain::ExecutionEventRecordV1 trade_event(
        std::uint64_t event_sequence,
        std::uint64_t trade_id,
        std::uint64_t incoming_order_id,
        std::uint64_t resting_order_id,
        domain::Side aggressor_side,
        std::int64_t price_ticks,
        std::int64_t quantity_lots)
    {
        domain::ExecutionEventRecordV1 event{};
        event.event_sequence = event_sequence;
        event.order_id = incoming_order_id;
        event.contra_order_id = resting_order_id;
        event.trade_id = trade_id;
        event.price_ticks = price_ticks;
        event.quantity_lots = quantity_lots;
        event.instrument_id = test_instrument_id;
        event.event_type = encode(domain::ExecutionEventType::TradeExecuted);
        event.side = encode(aggressor_side);
        return event;
    }

    bool apply_all(
        projections::MarketDataProjection& projection,
        const std::vector<domain::ExecutionEventRecordV1>& events)
    {
        for (const auto& event : events) {
            if (projection.apply(event).status == projections::ProjectionApplyStatus::Rejected) {
                return false;
            }
        }
        return true;
    }

    bool Passive_buy_adds_bid_level()
    {
        const std::vector events{
            rested_event(1, 1, domain::Side::Buy, 10000, 10)
        };
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.book().bids.size() == 1
            && projection.book().bids[0].price_ticks == 10000
            && projection.book().bids[0].quantity_lots == 10
            && projection.book().asks.empty();
    }

    bool Passive_sell_adds_ask_level()
    {
        const std::vector events{
            rested_event(1, 1, domain::Side::Sell, 10100, 5)
        };
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.book().asks.size() == 1
            && projection.book().asks[0].price_ticks == 10100
            && projection.book().asks[0].quantity_lots == 5
            && projection.book().bids.empty();
    }

    bool Trade_reduces_resting_level()
    {
        const std::vector events{
            rested_event(1, 1, domain::Side::Sell, 10100, 5),
            trade_event(2, 1, 2, 1, domain::Side::Buy, 10100, 3)
        };
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.trades().size() == 1
            && projection.book().asks.size() == 1
            && projection.book().asks[0].price_ticks == 10100
            && projection.book().asks[0].quantity_lots == 2;
    }

    bool Full_fill_removes_level()
    {
        const std::vector events{
            rested_event(1, 1, domain::Side::Sell, 10100, 5),
            trade_event(2, 1, 2, 1, domain::Side::Buy, 10100, 5)
        };
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.trades().size() == 1
            && projection.book().asks.empty()
            && projection.book().bids.empty();
    }

    bool Cancel_removes_order_from_projection()
    {
        const std::vector events{
            rested_event(1, 1, domain::Side::Buy, 10000, 10),
            cancelled_event(2, 1)
        };
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.book().bids.empty()
            && projection.book().asks.empty();
    }

    bool Replace_moves_order_to_new_price_and_new_id()
    {
        const std::vector events{
            rested_event(1, 1, domain::Side::Sell, 10200, 5),
            cancelled_event(2, 1),
            rested_event(3, 2, domain::Side::Sell, 10100, 3)
        };
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.book().asks.size() == 1
            && projection.book().asks[0].price_ticks == 10100
            && projection.book().asks[0].quantity_lots == 3;
    }

    bool Projection_rejects_event_sequence_gap()
    {
        projections::MarketDataProjection projection;
        if (projection.apply(accepted_event(1, 1)).status == projections::ProjectionApplyStatus::Rejected) {
            return false;
        }
        return projection.apply(rested_event(3, 1, domain::Side::Buy, 10000, 10)).status
                == projections::ProjectionApplyStatus::Rejected
            && projection.last_applied_event_sequence() == 1;
    }

    bool Projection_rebuilds_from_event_sequence()
    {
        const std::vector events{
            rested_event(1, 1, domain::Side::Buy, 10000, 10),
            rested_event(2, 2, domain::Side::Sell, 10100, 5),
            trade_event(3, 1, 3, 2, domain::Side::Buy, 10100, 5)
        };
        projections::MarketDataProjection projection;
        return apply_all(projection, events)
            && projection.trades().size() == 1
            && projection.trades()[0].incoming_order_id == 3
            && projection.trades()[0].resting_order_id == 2
            && projection.trades()[0].price_ticks == 10100
            && projection.trades()[0].quantity_lots == 5
            && projection.book().bids.size() == 1
            && projection.book().bids[0].price_ticks == 10000
            && projection.book().bids[0].quantity_lots == 10
            && projection.book().asks.empty();
    }
}

int main()
{
    if (!Passive_buy_adds_bid_level()) {
        return 1;
    }
    if (!Passive_sell_adds_ask_level()) {
        return 2;
    }
    if (!Trade_reduces_resting_level()) {
        return 3;
    }
    if (!Full_fill_removes_level()) {
        return 4;
    }
    if (!Cancel_removes_order_from_projection()) {
        return 5;
    }
    if (!Replace_moves_order_to_new_price_and_new_id()) {
        return 6;
    }
    if (!Projection_rejects_event_sequence_gap()) {
        return 7;
    }
    if (!Projection_rebuilds_from_event_sequence()) {
        return 8;
    }

    return 0;
}
