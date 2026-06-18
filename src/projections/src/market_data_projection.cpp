#include "projections/market_data_projection.hpp"

#include "core/matching_types.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace projections
{
    namespace
    {
        bool is_buy(std::uint16_t side) noexcept
        {
            return side == static_cast<std::uint16_t>(core::Side::Buy);
        }

        bool is_sell(std::uint16_t side) noexcept
        {
            return side == static_cast<std::uint16_t>(core::Side::Sell);
        }

        ProjectionApplyResult applied()
        {
            return {.status = ProjectionApplyStatus::Applied};
        }

        ProjectionApplyResult ignored()
        {
            return {.status = ProjectionApplyStatus::Ignored};
        }

        ProjectionApplyResult rejected(std::string error)
        {
            return {.status = ProjectionApplyStatus::Rejected, .error = std::move(error)};
        }

        std::uint16_t opposite_side(std::uint16_t aggressor_side) noexcept
        {
            if (is_buy(aggressor_side)) {
                return static_cast<std::uint16_t>(core::Side::Sell);
            }
            if (is_sell(aggressor_side)) {
                return static_cast<std::uint16_t>(core::Side::Buy);
            }
            return 0;
        }
    }

    ProjectionApplyResult MarketDataProjection::apply(const domain::ExecutionEventRecordV1& event)
    {
        if (event.event_sequence != last_applied_event_sequence_ + 1) {
            std::ostringstream out;
            out << "event sequence gap: expected " << (last_applied_event_sequence_ + 1)
                << " got " << event.event_sequence;
            return rejected(out.str());
        }

        ProjectionApplyResult result{};
        switch (static_cast<core::ExecutionEventType>(event.event_type)) {
        case core::ExecutionEventType::OrderAccepted:
        case core::ExecutionEventType::OrderRejected:
            result = ignored();
            break;
        case core::ExecutionEventType::OrderRested:
        case core::ExecutionEventType::OrderPartiallyFilled:
            result = apply_rested(event);
            break;
        case core::ExecutionEventType::TradeExecuted:
            result = apply_trade(event);
            break;
        case core::ExecutionEventType::OrderFullyFilled:
            result = apply_filled(event);
            break;
        case core::ExecutionEventType::OrderCancelled:
            result = apply_cancelled(event);
            break;
        default:
            result = rejected("unknown execution event type");
            break;
        }

        if (result.status != ProjectionApplyStatus::Rejected) {
            last_applied_event_sequence_ = event.event_sequence;
        }
        return result;
    }

    const PublicBookView& MarketDataProjection::book() const noexcept
    {
        return book_;
    }

    std::span<const PublicTrade> MarketDataProjection::trades() const noexcept
    {
        return trades_;
    }

    std::uint64_t MarketDataProjection::last_applied_event_sequence() const noexcept
    {
        return last_applied_event_sequence_;
    }

    ProjectionApplyResult MarketDataProjection::apply_rested(const domain::ExecutionEventRecordV1& event)
    {
        if (event.remaining_quantity_lots <= 0 || (!is_buy(event.side) && !is_sell(event.side))) {
            return rejected("resting event does not contain visible liquidity");
        }

        return upsert_order(
            event.order_id,
            event.instrument_id,
            event.side,
            event.price_ticks,
            event.remaining_quantity_lots);
    }

    ProjectionApplyResult MarketDataProjection::apply_trade(const domain::ExecutionEventRecordV1& event)
    {
        if (event.quantity_lots <= 0 || event.contra_order_id == 0) {
            return rejected("trade event is missing quantity or resting order id");
        }

        const auto found = active_orders_.find(event.contra_order_id);
        if (found == active_orders_.end()) {
            return rejected("trade references unknown resting order");
        }

        if (found->second.remaining_quantity_lots < event.quantity_lots) {
            return rejected("trade quantity exceeds projected resting quantity");
        }

        trades_.push_back(PublicTrade{
            .trade_id = event.trade_id,
            .instrument_id = event.instrument_id,
            .incoming_order_id = event.order_id,
            .resting_order_id = event.contra_order_id,
            .aggressor_side = event.side,
            .price_ticks = event.price_ticks,
            .quantity_lots = event.quantity_lots
        });

        found->second.remaining_quantity_lots -= event.quantity_lots;
        if (found->second.remaining_quantity_lots == 0) {
            active_orders_.erase(found);
        }

        rebuild_book();
        return applied();
    }

    ProjectionApplyResult MarketDataProjection::apply_cancelled(const domain::ExecutionEventRecordV1& event)
    {
        const auto found = active_orders_.find(event.order_id);
        if (found == active_orders_.end()) {
            return rejected("cancel references unknown resting order");
        }

        return remove_order(event.order_id);
    }

    ProjectionApplyResult MarketDataProjection::apply_filled(const domain::ExecutionEventRecordV1& event)
    {
        const auto found = active_orders_.find(event.order_id);
        if (found == active_orders_.end()) {
            return ignored();
        }

        return remove_order(event.order_id);
    }

    ProjectionApplyResult MarketDataProjection::upsert_order(
        std::uint64_t order_id,
        std::uint32_t instrument_id,
        std::uint16_t side,
        std::int64_t price_ticks,
        std::int64_t new_remaining_quantity_lots)
    {
        if (order_id == 0 || price_ticks <= 0 || new_remaining_quantity_lots <= 0) {
            return rejected("invalid order state for market data projection");
        }

        active_orders_[order_id] = OrderState{
            .instrument_id = instrument_id,
            .side = side,
            .price_ticks = price_ticks,
            .remaining_quantity_lots = new_remaining_quantity_lots
        };
        rebuild_book();
        return applied();
    }

    ProjectionApplyResult MarketDataProjection::remove_order(std::uint64_t order_id)
    {
        active_orders_.erase(order_id);
        rebuild_book();
        return applied();
    }

    void MarketDataProjection::rebuild_book()
    {
        std::map<std::int64_t, std::int64_t, std::greater<>> bids;
        std::map<std::int64_t, std::int64_t> asks;
        book_.instrument_id = 0;

        for (const auto& [_, order] : active_orders_) {
            book_.instrument_id = order.instrument_id;
            if (is_buy(order.side)) {
                bids[order.price_ticks] += order.remaining_quantity_lots;
            } else if (is_sell(order.side)) {
                asks[order.price_ticks] += order.remaining_quantity_lots;
            }
        }

        book_.bids.clear();
        book_.asks.clear();

        for (const auto& [price, quantity] : bids) {
            if (quantity > 0) {
                book_.bids.push_back({.price_ticks = price, .quantity_lots = quantity});
            }
        }
        for (const auto& [price, quantity] : asks) {
            if (quantity > 0) {
                book_.asks.push_back({.price_ticks = price, .quantity_lots = quantity});
            }
        }
    }
}
