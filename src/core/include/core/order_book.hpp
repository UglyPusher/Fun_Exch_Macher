#pragma once

/**
 * @file order_book.hpp
 * @brief Deterministic in-memory order book for one instrument.
 *
 * OrderBook owns price-time priority, active order indexing, event sequence
 * allocation, and trade id allocation. It must not perform I/O, parse scenarios,
 * call projections, or mix orders from different instruments.
 */

#include "core/matching_types.hpp"
#include "domain/execution_event_record.hpp"
#include "domain/order_command_record.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace core
{
    /**
     * @brief Deterministic reducer from order commands to execution events.
     *
     * The book mutates only its own in-memory state and returns durable
     * ExecutionEvent records to the caller. Event sequencing is part of the
     * replay contract and advances only when an event is emitted.
     */
    class OrderBook
    {
    public:
        /**
         * @brief Creates an empty book with the next event sequence initialized.
         */
        explicit OrderBook(std::uint64_t first_event_sequence = 1) noexcept;

        /**
         * @brief Validates, matches, and possibly rests a new order command.
         */
        [[nodiscard]] std::vector<domain::ExecutionEventRecordV1> apply_new_order(
            const domain::OrderCommandRecordV1& command);
        /**
         * @brief Cancels an active resting order without changing other book state.
         */
        [[nodiscard]] std::vector<domain::ExecutionEventRecordV1> apply_cancel_order(
            const domain::OrderCommandRecordV1& command);
        /**
         * @brief Replaces an active order as cancel-old plus submit-new.
         */
        [[nodiscard]] std::vector<domain::ExecutionEventRecordV1> apply_replace_order(
            const domain::OrderCommandRecordV1& command);
        /**
         * @brief Emits an unsupported-command rejection without mutating the book.
         */
        [[nodiscard]] std::vector<domain::ExecutionEventRecordV1> reject_unsupported_command(
            const domain::OrderCommandRecordV1& command);

        /**
         * @brief Checks whether an order id is currently active in the book.
         */
        [[nodiscard]] bool has_order(std::uint64_t order_id) const;
        /**
         * @brief Returns the current best bid price, or zero when no bid exists.
         */
        [[nodiscard]] std::int64_t best_bid_price() const;
        /**
         * @brief Returns the current best ask price, or zero when no ask exists.
         */
        [[nodiscard]] std::int64_t best_ask_price() const;
        /**
         * @brief Returns active remaining quantity for an order id, or zero when absent.
         */
        [[nodiscard]] std::int64_t remaining_quantity(std::uint64_t order_id) const;
        /**
         * @brief Returns the number of active resting orders.
         */
        [[nodiscard]] std::size_t active_order_count() const noexcept;
        /**
         * @brief Verifies that active index and price levels describe the same orders.
         */
        [[nodiscard]] bool validate_invariants() const;
        /**
         * @brief Builds a compact diagnostic snapshot for replay failure reports.
         */
        [[nodiscard]] std::string snapshot() const;

    private:
        struct RestingOrder
        {
            std::uint64_t order_id = 0;
            std::uint64_t client_id = 0;
            std::uint32_t instrument_id = 0;
            std::uint16_t side = 0;
            std::int64_t price_ticks = 0;
            std::int64_t remaining_quantity_lots = 0;
        };

        using BidLevels = std::map<std::int64_t, std::deque<RestingOrder>, std::greater<>>;
        using AskLevels = std::map<std::int64_t, std::deque<RestingOrder>>;

        [[nodiscard]] std::optional<Side> decode_side(std::uint16_t side) const noexcept;
        [[nodiscard]] std::optional<TimeInForce> decode_time_in_force(
            const domain::OrderCommandRecordV1& command) const noexcept;

        [[nodiscard]] RejectionReason validate_new_order(const domain::OrderCommandRecordV1& command) const noexcept;
        [[nodiscard]] RejectionReason validate_replace_order(const domain::OrderCommandRecordV1& command) const noexcept;
        [[nodiscard]] RejectionReason validate_order_book_instrument(
            const domain::OrderCommandRecordV1& command) const noexcept;

        void bind_instrument_if_needed(std::uint32_t instrument_id) noexcept;

        std::int64_t match_buy_order_against_asks(
            const domain::OrderCommandRecordV1& command,
            std::vector<domain::ExecutionEventRecordV1>& events);
        std::int64_t match_sell_order_against_bids(
            const domain::OrderCommandRecordV1& command,
            std::vector<domain::ExecutionEventRecordV1>& events);

        [[nodiscard]] domain::ExecutionEventRecordV1 emit_order_event(
            const domain::OrderCommandRecordV1& command,
            ExecutionEventType event_type,
            std::int64_t quantity_lots,
            std::int64_t remaining_quantity_lots) noexcept;
        [[nodiscard]] domain::ExecutionEventRecordV1 emit_trade_event(
            const domain::OrderCommandRecordV1& command,
            const RestingOrder& resting_order,
            std::int64_t trade_price_ticks,
            std::int64_t trade_quantity_lots,
            std::int64_t incoming_remaining_lots) noexcept;
        [[nodiscard]] domain::ExecutionEventRecordV1 emit_rejection_event(
            const domain::OrderCommandRecordV1& command,
            RejectionReason reason) noexcept;
        [[nodiscard]] domain::ExecutionEventRecordV1 emit_cancel_event(
            const domain::OrderCommandRecordV1& command,
            const RestingOrder& resting_order) noexcept;

        void rest_order(const domain::OrderCommandRecordV1& command, std::int64_t remaining_quantity_lots);
        void remove_cancelled_order(const RestingOrder& resting_order);
        [[nodiscard]] std::optional<RestingOrder> find_order(std::uint64_t order_id) const;
        [[nodiscard]] bool validate_bid_levels(std::unordered_set<std::uint64_t>& queued_order_ids) const;
        [[nodiscard]] bool validate_ask_levels(std::unordered_set<std::uint64_t>& queued_order_ids) const;
        [[nodiscard]] bool validate_price_levels_against_active_orders(
            const BidLevels& price_levels,
            Side expected_side,
            std::unordered_set<std::uint64_t>& queued_order_ids) const;
        [[nodiscard]] bool validate_price_levels_against_active_orders(
            const AskLevels& price_levels,
            Side expected_side,
            std::unordered_set<std::uint64_t>& queued_order_ids) const;
        [[nodiscard]] bool validate_resting_order_against_active_index(
            const RestingOrder& resting_order,
            std::int64_t price_ticks,
            Side expected_side,
            std::unordered_set<std::uint64_t>& queued_order_ids) const;
        [[nodiscard]] bool validate_active_orders_are_queued(
            const std::unordered_set<std::uint64_t>& queued_order_ids) const;

        template <typename TLevels>
        void erase_order_from_price_levels(TLevels& levels, const RestingOrder& resting_order);

    private:
        std::optional<std::uint32_t> instrument_id_;
        std::uint64_t next_event_sequence_ = 1;
        std::uint64_t next_trade_id_ = 1;
        BidLevels bids_;
        AskLevels asks_;
        std::unordered_map<std::uint64_t, RestingOrder> active_orders_;
    };
}
