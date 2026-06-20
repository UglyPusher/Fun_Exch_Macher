#pragma once

/**
 * @file market_data_projection.hpp
 * @brief Public market-data view rebuilt from execution events.
 *
 * MarketDataProjection consumes ExecutionEventRecordV1 values only. It must not
 * read Command WAL records, call the matcher, or decide whether commands are
 * valid.
 */

#include "domain/execution_event_record.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace projections
{
    /**
     * @brief Aggregated visible quantity at one public price level.
     */
    struct PublicPriceLevel
    {
        std::int64_t price_ticks = 0;
        std::int64_t quantity_lots = 0;
    };

    /**
     * @brief Public book view derived from active projected orders.
     *
     * Bids are ordered descending by price and asks ascending by price.
     */
    struct PublicBookView
    {
        std::uint32_t instrument_id = 0;
        std::vector<PublicPriceLevel> bids;
        std::vector<PublicPriceLevel> asks;
    };

    /**
     * @brief Public trade row copied from a TradeExecuted event.
     */
    struct PublicTrade
    {
        std::uint64_t trade_id = 0;
        std::uint32_t instrument_id = 0;
        std::uint64_t incoming_order_id = 0;
        std::uint64_t resting_order_id = 0;
        std::uint16_t aggressor_side = 0;
        std::int64_t price_ticks = 0;
        std::int64_t quantity_lots = 0;
    };

    /**
     * @brief Result status for applying one event to a projection.
     */
    enum class ProjectionApplyStatus
    {
        Applied,
        Ignored,
        Rejected
    };

    /**
     * @brief Result of applying one execution event to the projection.
     */
    struct ProjectionApplyResult
    {
        ProjectionApplyStatus status = ProjectionApplyStatus::Rejected;
        std::string error;
    };

    /**
     * @brief Reduces execution events into public book and trade views.
     *
     * Events must be applied in contiguous event_sequence order. Rejected
     * application does not advance the last applied sequence.
     */
    class MarketDataProjection
    {
    public:
        /**
         * @brief Applies one execution event to the projected public state.
         */
        [[nodiscard]] ProjectionApplyResult apply(const domain::ExecutionEventRecordV1& event);

        /**
         * @brief Returns the latest aggregated public book.
         */
        [[nodiscard]] const PublicBookView& book() const noexcept;
        /**
         * @brief Returns the projected public trade tape.
         */
        [[nodiscard]] std::span<const PublicTrade> trades() const noexcept;
        /**
         * @brief Returns the last event sequence accepted or ignored by this projection.
         */
        [[nodiscard]] std::uint64_t last_applied_event_sequence() const noexcept;

    private:
        struct OrderState
        {
            std::uint32_t instrument_id = 0;
            std::uint16_t side = 0;
            std::int64_t price_ticks = 0;
            std::int64_t remaining_quantity_lots = 0;
        };

        [[nodiscard]] ProjectionApplyResult apply_rested(const domain::ExecutionEventRecordV1& event);
        [[nodiscard]] ProjectionApplyResult apply_trade(const domain::ExecutionEventRecordV1& event);
        [[nodiscard]] ProjectionApplyResult apply_cancelled(const domain::ExecutionEventRecordV1& event);
        [[nodiscard]] ProjectionApplyResult apply_filled(const domain::ExecutionEventRecordV1& event);
        [[nodiscard]] ProjectionApplyResult upsert_order(
            std::uint64_t order_id,
            std::uint32_t instrument_id,
            std::uint16_t side,
            std::int64_t price_ticks,
            std::int64_t new_remaining_quantity_lots);
        [[nodiscard]] ProjectionApplyResult remove_order(std::uint64_t order_id);
        void rebuild_book();

    private:
        PublicBookView book_;
        std::vector<PublicTrade> trades_;
        std::uint64_t last_applied_event_sequence_ = 0;
        std::unordered_map<std::uint64_t, OrderState> active_orders_;
    };
}
