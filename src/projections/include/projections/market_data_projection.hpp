#pragma once

#include "domain/execution_event_record.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace projections
{
    struct PublicPriceLevel
    {
        std::int64_t price_ticks = 0;
        std::int64_t quantity_lots = 0;
    };

    struct PublicBookView
    {
        std::uint32_t instrument_id = 0;
        std::vector<PublicPriceLevel> bids;
        std::vector<PublicPriceLevel> asks;
    };

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

    enum class ProjectionApplyStatus
    {
        Applied,
        Ignored,
        Rejected
    };

    struct ProjectionApplyResult
    {
        ProjectionApplyStatus status = ProjectionApplyStatus::Rejected;
        std::string error;
    };

    class MarketDataProjection
    {
    public:
        [[nodiscard]] ProjectionApplyResult apply(const domain::ExecutionEventRecordV1& event);

        [[nodiscard]] const PublicBookView& book() const noexcept;
        [[nodiscard]] std::span<const PublicTrade> trades() const noexcept;
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
