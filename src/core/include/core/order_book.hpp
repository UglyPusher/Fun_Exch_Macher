#pragma once

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
#include <vector>

namespace core
{
    class OrderBook
    {
    public:
        explicit OrderBook(std::uint64_t first_event_sequence = 1) noexcept;

        [[nodiscard]] std::vector<domain::ExecutionEventRecordV1> apply_new_order(
            const domain::OrderCommandRecordV1& command);

        [[nodiscard]] bool has_order(std::uint64_t order_id) const;
        [[nodiscard]] std::int64_t best_bid_price() const;
        [[nodiscard]] std::int64_t best_ask_price() const;
        [[nodiscard]] std::int64_t remaining_quantity(std::uint64_t order_id) const;
        [[nodiscard]] std::size_t active_order_count() const noexcept;
        [[nodiscard]] bool validate_invariants() const;
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

        [[nodiscard]] RejectionReason validate_new_order(const domain::OrderCommandRecordV1& command) const noexcept;
        [[nodiscard]] domain::ExecutionEventRecordV1 make_event(
            const domain::OrderCommandRecordV1& command,
            ExecutionEventType event_type,
            std::int64_t quantity_lots,
            std::int64_t remaining_quantity_lots) noexcept;
        [[nodiscard]] domain::ExecutionEventRecordV1 make_trade_event(
            const domain::OrderCommandRecordV1& command,
            const RestingOrder& resting_order,
            std::int64_t trade_price_ticks,
            std::int64_t trade_quantity_lots,
            std::int64_t incoming_remaining_lots) noexcept;
        [[nodiscard]] domain::ExecutionEventRecordV1 make_rejected_event(
            const domain::OrderCommandRecordV1& command,
            RejectionReason reason) noexcept;

        void rest_order(const domain::OrderCommandRecordV1& command, std::int64_t remaining_quantity_lots);
        void remove_resting_order(const RestingOrder& resting_order);
        [[nodiscard]] std::optional<RestingOrder> find_order(std::uint64_t order_id) const;

        template <typename TLevels>
        void remove_empty_best_level(TLevels& levels)
        {
            if (!levels.empty() && levels.begin()->second.empty()) {
                levels.erase(levels.begin());
            }
        }

    private:
        std::uint64_t next_event_sequence_ = 1;
        std::uint64_t next_trade_id_ = 1;
        BidLevels bids_;
        AskLevels asks_;
        std::unordered_map<std::uint64_t, RestingOrder> active_orders_;
    };
}
