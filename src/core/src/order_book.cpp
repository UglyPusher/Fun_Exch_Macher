#include "core/order_book.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace core
{
    namespace
    {
        bool is_buy(std::uint16_t side) noexcept
        {
            return side == static_cast<std::uint16_t>(Side::Buy);
        }

        bool is_sell(std::uint16_t side) noexcept
        {
            return side == static_cast<std::uint16_t>(Side::Sell);
        }
    }

    OrderBook::OrderBook(std::uint64_t first_event_sequence) noexcept
        : next_event_sequence_(first_event_sequence)
    {
    }

    std::vector<domain::ExecutionEventRecordV1> OrderBook::apply_new_order(
        const domain::OrderCommandRecordV1& command)
    {
        std::vector<domain::ExecutionEventRecordV1> events;

        const auto rejection_reason = validate_new_order(command);
        if (rejection_reason != RejectionReason::None) {
            events.push_back(make_rejected_event(command, rejection_reason));
            return events;
        }

        events.push_back(make_event(command, ExecutionEventType::OrderAccepted, command.quantity_lots, command.quantity_lots));

        auto incoming_remaining = command.quantity_lots;
        auto had_trade = false;

        if (is_buy(command.side)) {
            while (incoming_remaining > 0 && !asks_.empty()) {
                auto& [best_price, orders] = *asks_.begin();
                if (command.price_ticks < best_price) {
                    break;
                }

                auto& resting = orders.front();
                const auto trade_quantity = std::min(incoming_remaining, resting.remaining_quantity_lots);
                incoming_remaining -= trade_quantity;
                resting.remaining_quantity_lots -= trade_quantity;
                active_orders_[resting.order_id] = resting;
                had_trade = true;
                events.push_back(make_trade_event(command, resting, best_price, trade_quantity, incoming_remaining));

                if (resting.remaining_quantity_lots == 0) {
                    remove_resting_order(resting);
                    orders.pop_front();
                    remove_empty_best_level(asks_);
                }
            }
        } else {
            while (incoming_remaining > 0 && !bids_.empty()) {
                auto& [best_price, orders] = *bids_.begin();
                if (command.price_ticks > best_price) {
                    break;
                }

                auto& resting = orders.front();
                const auto trade_quantity = std::min(incoming_remaining, resting.remaining_quantity_lots);
                incoming_remaining -= trade_quantity;
                resting.remaining_quantity_lots -= trade_quantity;
                active_orders_[resting.order_id] = resting;
                had_trade = true;
                events.push_back(make_trade_event(command, resting, best_price, trade_quantity, incoming_remaining));

                if (resting.remaining_quantity_lots == 0) {
                    remove_resting_order(resting);
                    orders.pop_front();
                    remove_empty_best_level(bids_);
                }
            }
        }

        if (incoming_remaining == 0) {
            events.push_back(make_event(command, ExecutionEventType::OrderFullyFilled, command.quantity_lots, 0));
            return events;
        }

        rest_order(command, incoming_remaining);
        events.push_back(make_event(
            command,
            had_trade ? ExecutionEventType::OrderPartiallyFilled : ExecutionEventType::OrderRested,
            command.quantity_lots - incoming_remaining,
            incoming_remaining));
        return events;
    }

    std::vector<domain::ExecutionEventRecordV1> OrderBook::apply_cancel_order(
        const domain::OrderCommandRecordV1& command)
    {
        std::vector<domain::ExecutionEventRecordV1> events;

        if (command.command_type != static_cast<std::uint16_t>(CommandType::CancelOrder)) {
            events.push_back(make_rejected_event(command, RejectionReason::UnsupportedCommand));
            return events;
        }

        const auto resting_order = find_order(command.order_id);
        if (!resting_order.has_value()) {
            events.push_back(make_rejected_event(command, RejectionReason::UnknownOrderId));
            return events;
        }

        if (resting_order->instrument_id != command.instrument_id) {
            events.push_back(make_rejected_event(command, RejectionReason::InstrumentMismatch));
            return events;
        }

        remove_cancelled_order(*resting_order);
        events.push_back(make_cancelled_event(command, *resting_order));
        return events;
    }

    bool OrderBook::has_order(std::uint64_t order_id) const
    {
        return active_orders_.contains(order_id);
    }

    std::int64_t OrderBook::best_bid_price() const
    {
        return bids_.empty() ? 0 : bids_.begin()->first;
    }

    std::int64_t OrderBook::best_ask_price() const
    {
        return asks_.empty() ? 0 : asks_.begin()->first;
    }

    std::int64_t OrderBook::remaining_quantity(std::uint64_t order_id) const
    {
        const auto found = active_orders_.find(order_id);
        return found == active_orders_.end() ? 0 : found->second.remaining_quantity_lots;
    }

    std::size_t OrderBook::active_order_count() const noexcept
    {
        return active_orders_.size();
    }

    bool OrderBook::validate_invariants() const
    {
        std::unordered_set<std::uint64_t> queued_order_ids;

        const auto validate_levels = [this, &queued_order_ids](const auto& levels, std::uint16_t side) {
            for (const auto& [price, orders] : levels) {
                if (orders.empty() || price <= 0) {
                    return false;
                }

                for (const auto& order : orders) {
                    if (order.side != side
                        || order.price_ticks != price
                        || order.remaining_quantity_lots <= 0
                        || !queued_order_ids.insert(order.order_id).second) {
                        return false;
                    }

                    const auto active = active_orders_.find(order.order_id);
                    if (active == active_orders_.end()
                        || active->second.client_id != order.client_id
                        || active->second.instrument_id != order.instrument_id
                        || active->second.side != order.side
                        || active->second.price_ticks != order.price_ticks
                        || active->second.remaining_quantity_lots != order.remaining_quantity_lots) {
                        return false;
                    }
                }
            }
            return true;
        };

        if (!validate_levels(bids_, static_cast<std::uint16_t>(Side::Buy))
            || !validate_levels(asks_, static_cast<std::uint16_t>(Side::Sell))) {
            return false;
        }

        if (queued_order_ids.size() != active_orders_.size()) {
            return false;
        }

        for (const auto& [order_id, order] : active_orders_) {
            if (!queued_order_ids.contains(order_id) || order.remaining_quantity_lots <= 0) {
                return false;
            }
        }

        return bids_.empty() || asks_.empty() || bids_.begin()->first < asks_.begin()->first;
    }

    std::string OrderBook::snapshot() const
    {
        std::ostringstream out;
        out << "active_orders=" << active_orders_.size()
            << " best_bid=" << best_bid_price()
            << " best_ask=" << best_ask_price();
        return out.str();
    }

    RejectionReason OrderBook::validate_new_order(const domain::OrderCommandRecordV1& command) const noexcept
    {
        if (command.command_type != static_cast<std::uint16_t>(CommandType::NewOrder)) {
            return RejectionReason::UnsupportedCommand;
        }

        if (!is_buy(command.side) && !is_sell(command.side)) {
            return RejectionReason::InvalidSide;
        }

        if (command.price_ticks <= 0) {
            return RejectionReason::InvalidPrice;
        }

        if (command.quantity_lots <= 0) {
            return RejectionReason::InvalidQuantity;
        }

        if (active_orders_.contains(command.order_id)) {
            return RejectionReason::DuplicateOrderId;
        }

        if (command.time_in_force != static_cast<std::uint16_t>(TimeInForce::Gtc)) {
            return RejectionReason::UnsupportedTimeInForce;
        }

        return RejectionReason::None;
    }

    domain::ExecutionEventRecordV1 OrderBook::make_event(
        const domain::OrderCommandRecordV1& command,
        ExecutionEventType event_type,
        std::int64_t quantity_lots,
        std::int64_t remaining_quantity_lots) noexcept
    {
        domain::ExecutionEventRecordV1 event{};
        event.event_sequence = next_event_sequence_++;
        event.command_sequence = command.command_sequence;
        event.source_ingress_epoch = command.source_ingress_epoch;
        event.source_ingress_sequence = command.source_ingress_sequence;
        event.order_id = command.order_id;
        event.price_ticks = command.price_ticks;
        event.quantity_lots = quantity_lots;
        event.remaining_quantity_lots = remaining_quantity_lots;
        event.instrument_id = command.instrument_id;
        event.event_type = static_cast<std::uint16_t>(event_type);
        event.side = command.side;
        return event;
    }

    domain::ExecutionEventRecordV1 OrderBook::make_trade_event(
        const domain::OrderCommandRecordV1& command,
        const RestingOrder& resting_order,
        std::int64_t trade_price_ticks,
        std::int64_t trade_quantity_lots,
        std::int64_t incoming_remaining_lots) noexcept
    {
        auto event = make_event(command, ExecutionEventType::TradeExecuted, trade_quantity_lots, incoming_remaining_lots);
        event.contra_order_id = resting_order.order_id;
        event.trade_id = next_trade_id_++;
        event.price_ticks = trade_price_ticks;
        return event;
    }

    domain::ExecutionEventRecordV1 OrderBook::make_rejected_event(
        const domain::OrderCommandRecordV1& command,
        RejectionReason reason) noexcept
    {
        auto event = make_event(command, ExecutionEventType::OrderRejected, 0, 0);
        event.rejection_reason = static_cast<std::uint16_t>(reason);
        return event;
    }

    domain::ExecutionEventRecordV1 OrderBook::make_cancelled_event(
        const domain::OrderCommandRecordV1& command,
        const RestingOrder& resting_order) noexcept
    {
        auto event = make_event(command, ExecutionEventType::OrderCancelled, 0, resting_order.remaining_quantity_lots);
        event.price_ticks = resting_order.price_ticks;
        event.side = resting_order.side;
        event.instrument_id = resting_order.instrument_id;
        return event;
    }

    void OrderBook::rest_order(const domain::OrderCommandRecordV1& command, std::int64_t remaining_quantity_lots)
    {
        RestingOrder resting_order{
            .order_id = command.order_id,
            .client_id = command.client_id,
            .instrument_id = command.instrument_id,
            .side = command.side,
            .price_ticks = command.price_ticks,
            .remaining_quantity_lots = remaining_quantity_lots
        };

        if (is_buy(command.side)) {
            bids_[command.price_ticks].push_back(resting_order);
        } else {
            asks_[command.price_ticks].push_back(resting_order);
        }
        active_orders_[command.order_id] = resting_order;
    }

    void OrderBook::remove_resting_order(const RestingOrder& resting_order)
    {
        active_orders_.erase(resting_order.order_id);
    }

    void OrderBook::remove_cancelled_order(const RestingOrder& resting_order)
    {
        const auto erase_from_level = [&resting_order](auto& levels) {
            const auto level = levels.find(resting_order.price_ticks);
            if (level == levels.end()) {
                return;
            }

            auto& orders = level->second;
            const auto found = std::find_if(orders.begin(), orders.end(), [&resting_order](const auto& order) {
                return order.order_id == resting_order.order_id;
            });
            if (found != orders.end()) {
                orders.erase(found);
            }
            if (orders.empty()) {
                levels.erase(level);
            }
        };

        if (is_buy(resting_order.side)) {
            erase_from_level(bids_);
        } else {
            erase_from_level(asks_);
        }

        active_orders_.erase(resting_order.order_id);
    }

    std::optional<OrderBook::RestingOrder> OrderBook::find_order(std::uint64_t order_id) const
    {
        const auto found = active_orders_.find(order_id);
        if (found == active_orders_.end()) {
            return std::nullopt;
        }
        return found->second;
    }
}
