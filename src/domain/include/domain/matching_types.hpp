#pragma once

/**
 * @file matching_types.hpp
 * @brief Durable matching enum values shared by commands and events.
 *
 * These numeric assignments are part of the WAL payload contract. Do not
 * reorder or reuse values without a versioned migration plan.
 */

#include <cstdint>

namespace domain
{
    /**
     * @brief Command kinds accepted by the current matcher.
     */
    enum class CommandType : std::uint16_t
    {
        NewOrder = 1,
        CancelOrder = 2,
        ReplaceOrder = 3
    };

    /**
     * @brief Order side stored in command and event records.
     */
    enum class Side : std::uint16_t
    {
        Buy = 1,
        Sell = 2
    };

    /**
     * @brief Time-in-force values supported by the current prototype.
     */
    enum class TimeInForce : std::uint16_t
    {
        Gtc = 1
    };

    /**
     * @brief Event kinds emitted by deterministic matching.
     */
    enum class ExecutionEventType : std::uint16_t
    {
        OrderAccepted = 1,
        OrderRejected = 2,
        TradeExecuted = 3,
        OrderRested = 4,
        OrderPartiallyFilled = 5,
        OrderFullyFilled = 6,
        OrderCancelled = 7
    };

    /**
     * @brief Rejection reasons emitted for commands that do not mutate the book.
     */
    enum class RejectionReason : std::uint16_t
    {
        None = 0,
        UnsupportedCommand = 1,
        InvalidSide = 2,
        InvalidPrice = 3,
        InvalidQuantity = 4,
        DuplicateOrderId = 5,
        UnsupportedTimeInForce = 6,
        UnknownOrderId = 7,
        InstrumentMismatch = 8,
        ReplaceWouldDuplicateOrderId = 9,
        InvalidReplacementOrderId = 10
    };
}
