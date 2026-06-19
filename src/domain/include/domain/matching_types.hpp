#pragma once

// Defines the wire-level matching enums shared by commands, execution events,
// core matching logic, replay, and projections.

#include <cstdint>

namespace domain
{
    enum class CommandType : std::uint16_t
    {
        NewOrder = 1,
        CancelOrder = 2,
        ReplaceOrder = 3
    };

    enum class Side : std::uint16_t
    {
        Buy = 1,
        Sell = 2
    };

    enum class TimeInForce : std::uint16_t
    {
        Gtc = 1
    };

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
