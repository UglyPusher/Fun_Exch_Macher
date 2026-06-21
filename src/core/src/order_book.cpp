/**
 * @file order_book.cpp
 * @brief Implements deterministic price-time matching for one instrument.
 *
 * Event and trade sequences are part of replay, so all sequence allocation
 * stays inside this module and advances only when an event is emitted.
 */

#include "core/order_book.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace core
{
    namespace
    {
        bool is_buy(Side side) noexcept
        {
            return side == Side::Buy;
        }

        bool is_sell(Side side) noexcept
        {
            return side == Side::Sell;
        }

        std::uint16_t encode_command_type(CommandType command_type) noexcept
        {
            return static_cast<std::uint16_t>(command_type);
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
            events.push_back(emit_rejection_event(command, rejection_reason));
            return events;
        }

        bind_instrument_if_needed(command.instrument_id);

        events.push_back(emit_order_event(
            command,
            ExecutionEventType::OrderAccepted,
            command.quantity_lots,
            command.quantity_lots));

        std::int64_t incoming_remaining_quantity_lots = command.quantity_lots;
        const std::optional<Side> command_side = decode_side(command.side);
        if (command_side == Side::Buy) {
            incoming_remaining_quantity_lots = match_buy_order_against_asks(command, events);
        } else {
            incoming_remaining_quantity_lots = match_sell_order_against_bids(command, events);
        }

        if (incoming_remaining_quantity_lots == 0) {
            events.push_back(emit_order_event(
                command,
                ExecutionEventType::OrderFullyFilled,
                command.quantity_lots,
                0));
            return events;
        }

        rest_order(command, incoming_remaining_quantity_lots);
        const bool has_trade = incoming_remaining_quantity_lots != command.quantity_lots;
        events.push_back(emit_order_event(
            command,
            has_trade ? ExecutionEventType::OrderPartiallyFilled : ExecutionEventType::OrderRested,
            command.quantity_lots - incoming_remaining_quantity_lots,
            incoming_remaining_quantity_lots));
        return events;
    }

    std::vector<domain::ExecutionEventRecordV1> OrderBook::apply_cancel_order(
        const domain::OrderCommandRecordV1& command)
    {
        std::vector<domain::ExecutionEventRecordV1> events;

        if (decode_command_type(command.command_type) != CommandType::CancelOrder) {
            events.push_back(emit_rejection_event(command, RejectionReason::UnsupportedCommand));
            return events;
        }

        const auto resting_order = find_order(command.order_id);
        if (!resting_order.has_value()) {
            events.push_back(emit_rejection_event(command, RejectionReason::UnknownOrderId));
            return events;
        }

        if (resting_order->instrument_id != command.instrument_id) {
            events.push_back(emit_rejection_event(command, RejectionReason::InstrumentMismatch));
            return events;
        }

        remove_cancelled_order(*resting_order);
        events.push_back(emit_cancel_event(command, *resting_order));
        return events;
    }

    std::vector<domain::ExecutionEventRecordV1> OrderBook::apply_replace_order(
        const domain::OrderCommandRecordV1& command)
    {
        std::vector<domain::ExecutionEventRecordV1> events;

        const auto rejection_reason = validate_replace_order(command);
        if (rejection_reason != RejectionReason::None) {
            events.push_back(emit_rejection_event(command, rejection_reason));
            return events;
        }

        const auto resting_order = find_order(command.order_id);
        remove_cancelled_order(*resting_order);
        events.push_back(emit_cancel_event(command, *resting_order));

        domain::OrderCommandRecordV1 replacement_new_order_command = command;
        replacement_new_order_command.command_type = encode_command_type(CommandType::NewOrder);
        replacement_new_order_command.order_id = command.replacement_order_id;

        std::vector<domain::ExecutionEventRecordV1> replacement_events = apply_new_order(replacement_new_order_command);
        events.insert(events.end(), replacement_events.begin(), replacement_events.end());
        return events;
    }

    std::vector<domain::ExecutionEventRecordV1> OrderBook::reject_unsupported_command(
        const domain::OrderCommandRecordV1& command)
    {
        return {emit_rejection_event(command, RejectionReason::UnsupportedCommand)};
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
        const auto active_order_position = active_orders_.find(order_id);
        return active_order_position == active_orders_.end() ? 0 : active_order_position->second.remaining_quantity_lots;
    }

    std::size_t OrderBook::active_order_count() const noexcept
    {
        return active_orders_.size();
    }

    bool OrderBook::validate_invariants() const
    {
        std::unordered_set<std::uint64_t> queued_order_ids;

        if (!validate_bid_levels(queued_order_ids) || !validate_ask_levels(queued_order_ids)) {
            return false;
        }

        if (!validate_active_orders_are_queued(queued_order_ids)) {
            return false;
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
        if (decode_command_type(command.command_type) != CommandType::NewOrder) {
            return RejectionReason::UnsupportedCommand;
        }

        const std::optional<Side> command_side = decode_side(command.side);
        if (!command_side.has_value() || (!is_buy(*command_side) && !is_sell(*command_side))) {
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

        const auto instrument_validation_result = validate_order_book_instrument(command);
        if (instrument_validation_result != RejectionReason::None) {
            return instrument_validation_result;
        }

        if (decode_time_in_force(command) != TimeInForce::Gtc) {
            return RejectionReason::UnsupportedTimeInForce;
        }

        return RejectionReason::None;
    }

    RejectionReason OrderBook::validate_replace_order(const domain::OrderCommandRecordV1& command) const noexcept
    {
        if (decode_command_type(command.command_type) != CommandType::ReplaceOrder) {
            return RejectionReason::UnsupportedCommand;
        }

        const auto resting_order = find_order(command.order_id);
        if (!resting_order.has_value()) {
            return RejectionReason::UnknownOrderId;
        }

        if (resting_order->instrument_id != command.instrument_id) {
            return RejectionReason::InstrumentMismatch;
        }

        if (command.replacement_order_id == 0 || command.replacement_order_id == command.order_id) {
            return RejectionReason::InvalidReplacementOrderId;
        }

        if (active_orders_.contains(command.replacement_order_id)) {
            return RejectionReason::ReplaceWouldDuplicateOrderId;
        }

        const std::optional<Side> command_side = decode_side(command.side);
        if (!command_side.has_value() || (!is_buy(*command_side) && !is_sell(*command_side))) {
            return RejectionReason::InvalidSide;
        }

        if (command.price_ticks <= 0) {
            return RejectionReason::InvalidPrice;
        }

        if (command.quantity_lots <= 0) {
            return RejectionReason::InvalidQuantity;
        }

        if (decode_time_in_force(command) != TimeInForce::Gtc) {
            return RejectionReason::UnsupportedTimeInForce;
        }

        return RejectionReason::None;
    }

    std::optional<Side> OrderBook::decode_side(std::uint16_t side) const noexcept
    {
        switch (static_cast<Side>(side)) {
        case Side::Buy:
        case Side::Sell:
            return static_cast<Side>(side);
        default:
            return std::nullopt;
        }
    }

    std::optional<TimeInForce> OrderBook::decode_time_in_force(
        const domain::OrderCommandRecordV1& command) const noexcept
    {
        switch (static_cast<TimeInForce>(command.time_in_force)) {
        case TimeInForce::Gtc:
            return TimeInForce::Gtc;
        default:
            return std::nullopt;
        }
    }

    RejectionReason OrderBook::validate_order_book_instrument(
        const domain::OrderCommandRecordV1& command) const noexcept
    {
        if (instrument_id_.has_value() && command.instrument_id != *instrument_id_) {
            return RejectionReason::InstrumentMismatch;
        }

        return RejectionReason::None;
    }

    void OrderBook::bind_instrument_if_needed(std::uint32_t instrument_id) noexcept
    {
        if (!instrument_id_.has_value()) {
            instrument_id_ = instrument_id;
        }
    }

    std::int64_t OrderBook::match_buy_order_against_asks(
        const domain::OrderCommandRecordV1& command,
        std::vector<domain::ExecutionEventRecordV1>& events)
    {
        std::int64_t incoming_remaining_quantity_lots = command.quantity_lots;

        while (incoming_remaining_quantity_lots > 0 && !asks_.empty()) {
            auto best_ask_level_position = asks_.begin();
            const std::int64_t best_ask_price_ticks = best_ask_level_position->first;
            if (command.price_ticks < best_ask_price_ticks) {
                break;
            }

            auto& resting_orders = best_ask_level_position->second;
            while (incoming_remaining_quantity_lots > 0 && !resting_orders.empty()) {
                RestingOrder& resting_order = resting_orders.front();
                const auto trade_quantity_lots = std::min(
                    incoming_remaining_quantity_lots,
                    resting_order.remaining_quantity_lots);

                incoming_remaining_quantity_lots -= trade_quantity_lots;
                resting_order.remaining_quantity_lots -= trade_quantity_lots;
                active_orders_[resting_order.order_id] = resting_order;

                events.push_back(emit_trade_event(
                    command,
                    resting_order,
                    best_ask_price_ticks,
                    trade_quantity_lots,
                    incoming_remaining_quantity_lots));

                if (resting_order.remaining_quantity_lots == 0) {
                    active_orders_.erase(resting_order.order_id);
                    resting_orders.pop_front();
                }
            }

            if (resting_orders.empty()) {
                asks_.erase(best_ask_level_position);
            }
        }

        return incoming_remaining_quantity_lots;
    }

    std::int64_t OrderBook::match_sell_order_against_bids(
        const domain::OrderCommandRecordV1& command,
        std::vector<domain::ExecutionEventRecordV1>& events)
    {
        std::int64_t incoming_remaining_quantity_lots = command.quantity_lots;

        while (incoming_remaining_quantity_lots > 0 && !bids_.empty()) {
            auto best_bid_level_position = bids_.begin();
            const std::int64_t best_bid_price_ticks = best_bid_level_position->first;
            if (command.price_ticks > best_bid_price_ticks) {
                break;
            }

            auto& resting_orders = best_bid_level_position->second;
            while (incoming_remaining_quantity_lots > 0 && !resting_orders.empty()) {
                RestingOrder& resting_order = resting_orders.front();
                const auto trade_quantity_lots = std::min(
                    incoming_remaining_quantity_lots,
                    resting_order.remaining_quantity_lots);

                incoming_remaining_quantity_lots -= trade_quantity_lots;
                resting_order.remaining_quantity_lots -= trade_quantity_lots;
                active_orders_[resting_order.order_id] = resting_order;

                events.push_back(emit_trade_event(
                    command,
                    resting_order,
                    best_bid_price_ticks,
                    trade_quantity_lots,
                    incoming_remaining_quantity_lots));

                if (resting_order.remaining_quantity_lots == 0) {
                    active_orders_.erase(resting_order.order_id);
                    resting_orders.pop_front();
                }
            }

            if (resting_orders.empty()) {
                bids_.erase(best_bid_level_position);
            }
        }

        return incoming_remaining_quantity_lots;
    }

    domain::ExecutionEventRecordV1 OrderBook::emit_order_event(
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

    domain::ExecutionEventRecordV1 OrderBook::emit_trade_event(
        const domain::OrderCommandRecordV1& command,
        const RestingOrder& resting_order,
        std::int64_t trade_price_ticks,
        std::int64_t trade_quantity_lots,
        std::int64_t incoming_remaining_lots) noexcept
    {
        domain::ExecutionEventRecordV1 event = emit_order_event(
            command,
            ExecutionEventType::TradeExecuted,
            trade_quantity_lots,
            incoming_remaining_lots);
        event.contra_order_id = resting_order.order_id;
        event.trade_id = next_trade_id_++;
        event.price_ticks = trade_price_ticks;
        return event;
    }

    domain::ExecutionEventRecordV1 OrderBook::emit_rejection_event(
        const domain::OrderCommandRecordV1& command,
        RejectionReason reason) noexcept
    {
        domain::ExecutionEventRecordV1 event =
            emit_order_event(command, ExecutionEventType::OrderRejected, 0, 0);
        event.rejection_reason = static_cast<std::uint16_t>(reason);
        return event;
    }

    domain::ExecutionEventRecordV1 OrderBook::emit_cancel_event(
        const domain::OrderCommandRecordV1& command,
        const RestingOrder& resting_order) noexcept
    {
        domain::ExecutionEventRecordV1 event = emit_order_event(
            command,
            ExecutionEventType::OrderCancelled,
            0,
            resting_order.remaining_quantity_lots);
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

        if (decode_side(command.side) == Side::Buy) {
            bids_[command.price_ticks].push_back(resting_order);
        } else {
            asks_[command.price_ticks].push_back(resting_order);
        }
        active_orders_[command.order_id] = resting_order;
    }

    template <typename TLevels>
    void OrderBook::erase_order_from_price_levels(TLevels& levels, const RestingOrder& resting_order)
    {
        const auto price_level_position = levels.find(resting_order.price_ticks);
        if (price_level_position == levels.end()) {
            return;
        }

        auto& resting_orders = price_level_position->second;
        const auto resting_order_position = std::find_if(
            resting_orders.begin(),
            resting_orders.end(),
            [&resting_order](const RestingOrder& order) {
                return order.order_id == resting_order.order_id;
            });

        if (resting_order_position != resting_orders.end()) {
            resting_orders.erase(resting_order_position);
        }
        if (resting_orders.empty()) {
            levels.erase(price_level_position);
        }
    }

    void OrderBook::remove_cancelled_order(const RestingOrder& resting_order)
    {
        if (decode_side(resting_order.side) == Side::Buy) {
            erase_order_from_price_levels(bids_, resting_order);
        } else {
            erase_order_from_price_levels(asks_, resting_order);
        }

        active_orders_.erase(resting_order.order_id);
    }

    std::optional<OrderBook::RestingOrder> OrderBook::find_order(std::uint64_t order_id) const
    {
        const auto active_order_position = active_orders_.find(order_id);
        if (active_order_position == active_orders_.end()) {
            return std::nullopt;
        }
        return active_order_position->second;
    }

    bool OrderBook::validate_bid_levels(std::unordered_set<std::uint64_t>& queued_order_ids) const
    {
        return validate_price_levels_against_active_orders(bids_, Side::Buy, queued_order_ids);
    }

    bool OrderBook::validate_ask_levels(std::unordered_set<std::uint64_t>& queued_order_ids) const
    {
        return validate_price_levels_against_active_orders(asks_, Side::Sell, queued_order_ids);
    }

    bool OrderBook::validate_price_levels_against_active_orders(
        const OrderBook::BidLevels& price_levels,
        Side expected_side,
        std::unordered_set<std::uint64_t>& queued_order_ids) const
    {
        for (const auto& [price_ticks, resting_orders] : price_levels) {
            if (resting_orders.empty() || price_ticks <= 0) {
                return false;
            }

            for (const RestingOrder& resting_order : resting_orders) {
                if (!validate_resting_order_against_active_index(
                        resting_order,
                        price_ticks,
                        expected_side,
                        queued_order_ids)) {
                    return false;
                }
            }
        }

        return true;
    }

    bool OrderBook::validate_price_levels_against_active_orders(
        const OrderBook::AskLevels& price_levels,
        Side expected_side,
        std::unordered_set<std::uint64_t>& queued_order_ids) const
    {
        for (const auto& [price_ticks, resting_orders] : price_levels) {
            if (resting_orders.empty() || price_ticks <= 0) {
                return false;
            }

            for (const RestingOrder& resting_order : resting_orders) {
                if (!validate_resting_order_against_active_index(
                        resting_order,
                        price_ticks,
                        expected_side,
                        queued_order_ids)) {
                    return false;
                }
            }
        }

        return true;
    }

    bool OrderBook::validate_resting_order_against_active_index(
        const OrderBook::RestingOrder& resting_order,
        std::int64_t price_ticks,
        Side expected_side,
        std::unordered_set<std::uint64_t>& queued_order_ids) const
    {
        if (decode_side(resting_order.side) != expected_side
            || resting_order.price_ticks != price_ticks
            || resting_order.remaining_quantity_lots <= 0
            || !queued_order_ids.insert(resting_order.order_id).second) {
            return false;
        }

        const auto active_order_position = active_orders_.find(resting_order.order_id);
        if (active_order_position == active_orders_.end()) {
            return false;
        }

        const RestingOrder& active_order = active_order_position->second;
        return active_order.client_id == resting_order.client_id
            && active_order.instrument_id == resting_order.instrument_id
            && active_order.side == resting_order.side
            && active_order.price_ticks == resting_order.price_ticks
            && active_order.remaining_quantity_lots == resting_order.remaining_quantity_lots;
    }

    bool OrderBook::validate_active_orders_are_queued(
        const std::unordered_set<std::uint64_t>& queued_order_ids) const
    {
        if (queued_order_ids.size() != active_orders_.size()) {
            return false;
        }

        for (const auto& [order_id, resting_order] : active_orders_) {
            if (!queued_order_ids.contains(order_id) || resting_order.remaining_quantity_lots <= 0) {
                return false;
            }
        }

        return true;
    }
}
