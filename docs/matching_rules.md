# Matching Rules

## 1. Purpose

This document defines the matching rules for the first prototype of the deterministic matching engine.

The goal is to specify a small, testable, deterministic rule set.

The first prototype supports:

* one or more instruments;
* limit buy orders;
* limit sell orders;
* price priority;
* FIFO priority within the same price level;
* partial fills;
* full fills;
* order cancellation;
* deterministic execution event generation.

The first prototype does not support:

* market orders;
* stop orders;
* iceberg orders;
* hidden liquidity;
* pegged orders;
* self-trade prevention;
* auctions;
* cross-instrument matching;
* advanced time-in-force policies;
* risk checks;
* order routing.

---

## 2. Core Model

The order book has two sides:

```text
bids: buy orders, sorted by price descending
asks: sell orders, sorted by price ascending
```

Each price level contains a FIFO queue of resting orders.

```text
OrderBook
├── bids
│   ├── price level 101.50
│   │   ├── order A
│   │   └── order B
│   └── price level 101.25
│       └── order C
└── asks
    ├── price level 101.75
    │   └── order D
    └── price level 102.00
        ├── order E
        └── order F
```

The matcher processes commands sequentially.

Only one command is applied to a given instrument order book at a time.

---

## 3. Definitions

## 3.1 Incoming Order

An incoming order is a new order currently being processed by the matcher.

It may:

* execute immediately;
* partially execute and leave a resting remainder;
* rest in the book without execution;
* be rejected.

## 3.2 Resting Order

A resting order is an active order already present in the order book.

It has:

* order id;
* client id;
* side;
* price;
* remaining quantity;
* FIFO position inside its price level.

## 3.3 Aggressive Order

An aggressive order is an incoming order that crosses the opposite side of the book.

A buy order is aggressive if:

```text
buy.price >= best_ask.price
```

A sell order is aggressive if:

```text
sell.price <= best_bid.price
```

## 3.4 Passive Order

A passive order is an incoming order that does not cross the opposite side and therefore rests in the book.

## 3.5 Price Level

A price level groups active resting orders with the same price.

The total visible quantity at a price level is the sum of remaining quantities of all active orders at that price.

---

## 4. Supported Commands

The first prototype supports three command types:

```cpp
enum class CommandType
{
    NewOrder,
    CancelOrder,
    ReplaceOrder
};
```

However, `ReplaceOrder` may be implemented after `NewOrder` and `CancelOrder`.

The minimum implementation order should be:

```text
1. NewOrder
2. CancelOrder
3. ReplaceOrder
```

---

## 5. Supported Order Types

The first prototype supports only limit orders.

A limit order contains:

```text
instrument id
client id
order id
side
limit price
quantity
time in force
```

Initial supported `TimeInForce`:

```text
GTC - Good Till Cancelled
```

Optional later additions:

```text
IOC - Immediate Or Cancel
FOK - Fill Or Kill
DAY - Day order
```

For the first version, assume all accepted resting orders are GTC.

---

## 6. Validation Rules

Before a command is applied, it must pass validation.

## 6.1 Common Command Validation

Reject the command if:

```text
instrument id is missing or unknown
command sequence is invalid
command type is unknown
client id is missing
order id is missing where required
```

## 6.2 New Order Validation

Reject a new order if:

```text
side is not Buy or Sell
price is less than or equal to zero
quantity is less than or equal to zero
order id is already active in the book
time in force is unsupported
```

## 6.3 Cancel Order Validation

Reject a cancel command if:

```text
order id is not active in the book
client id does not own the order, if ownership checks are enabled
instrument id does not match the active order
```

## 6.4 Replace Order Validation

Reject a replace command if:

```text
original order id is not active
new quantity is less than or equal to zero
new price is less than or equal to zero
replacement would create duplicate active order id
instrument id does not match the active order
client id does not own the order, if ownership checks are enabled
```

The current command DTO uses:

```text
order_id              old order id to replace
replacement_order_id  new order id for the replacement
price_ticks           new price
quantity_lots         new quantity
side                  replacement side
time_in_force         replacement TIF
```

---

## 6.5 NewOrder Event Sequencing

`NewOrder` processing emits deterministic events in this order.

Rejected order:

```text
OrderRejected
```

Accepted passive order that does not trade:

```text
OrderAccepted
OrderRested
```

Accepted aggressive order that fully fills:

```text
OrderAccepted
TradeExecuted        one event per matched resting order
OrderFullyFilled
```

Accepted aggressive order that partially fills and leaves a resting remainder:

```text
OrderAccepted
TradeExecuted        one event per matched resting order
OrderPartiallyFilled
```

For the first in-memory `OrderBook` implementation, `OrderPartiallyFilled` is the terminal event for the incoming order and implies that its remaining quantity is now resting in the book.

Every emitted event references the command that caused it:

```text
ExecutionEvent.command_sequence == OrderCommand.command_sequence
```

Trade events also carry:

```text
contra_order_id      resting order matched by the incoming order
trade_id             stream-local trade sequence
price_ticks          resting order price
quantity_lots        executed quantity
remaining_quantity   incoming order remaining quantity after this trade
```

---

## 7. Price Priority

The best bid is the highest buy price.

```text
best_bid = max(bid prices)
```

The best ask is the lowest sell price.

```text
best_ask = min(ask prices)
```

A buy order matches against the lowest ask price first.

A sell order matches against the highest bid price first.

Example:

```text
Asks:
101.00: A
101.25: B
101.50: C

Incoming buy limit 101.50
Execution order:
1. A at 101.00
2. B at 101.25
3. C at 101.50
```

---

## 8. FIFO Priority Within Price Level

Inside a price level, resting orders are matched in insertion order.

Example:

```text
Ask 101.00:
1. order A, qty 5
2. order B, qty 7
3. order C, qty 3

Incoming buy qty 10 at 101.00

Execution:
1. fill A for 5
2. partially fill B for 5

Remaining:
B qty 2
C qty 3
```

FIFO order must be stable and deterministic.

---

## 9. Crossing Rules

A buy order crosses the book if:

```text
incoming_buy.price >= best_ask.price
```

A sell order crosses the book if:

```text
incoming_sell.price <= best_bid.price
```

If the opposite side is empty, the order does not cross.

If the order does not cross and is allowed to rest, it is inserted into the book.

---

## 10. Execution Price Rule

For the first prototype, trades execute at the resting order price.

Example:

```text
Resting ask: 101.00
Incoming buy limit: 101.50
Trade price: 101.00
```

Example:

```text
Resting bid: 100.75
Incoming sell limit: 100.50
Trade price: 100.75
```

This rule is deterministic and common for continuous limit order books.

---

## 11. New Order Processing

Processing steps for a new limit order:

```text
1. Validate command.
2. If invalid, emit OrderRejected.
3. Create incoming order state.
4. Check opposite side of book.
5. While incoming quantity > 0 and crossing condition is true:
   - select best opposite price level;
   - select first resting order at that level;
   - execute min(incoming remaining quantity, resting remaining quantity);
   - emit TradeExecuted;
   - update remaining quantities;
   - if resting order is fully filled:
     - remove it from FIFO queue;
     - remove it from active order index;
     - emit OrderFullyFilled for resting order if required by event policy;
   - if incoming order is fully filled:
     - emit OrderFullyFilled for incoming order if required by event policy;
     - stop matching.
6. If incoming order still has remaining quantity:
   - insert it into its side of the book;
   - emit OrderAccepted or OrderPartiallyFilled according to event policy.
7. Remove empty price levels.
8. Verify invariants.
```

---

## 12. Event Policy

The exact event policy must be deterministic.

The recommended first prototype policy:

### 12.1 Accepted Passive Order

If a new order does not trade and rests in the book:

```text
OrderAccepted
```

### 12.2 Accepted Aggressive Order With Full Fill

If a new order trades immediately and is fully filled:

```text
OrderAccepted
TradeExecuted one or more times
OrderFullyFilled
```

### 12.3 Accepted Aggressive Order With Partial Fill and Resting Remainder

If a new order trades and then leaves a resting remainder:

```text
OrderAccepted
TradeExecuted one or more times
OrderPartiallyFilled
OrderRested
```

If `OrderRested` is not implemented, use:

```text
OrderAccepted
TradeExecuted one or more times
OrderPartiallyFilled
```

and ensure that the final event contains the remaining quantity.

### 12.4 Resting Order Fully Filled

When a resting order is fully filled by an incoming order:

```text
TradeExecuted
OrderFullyFilled for resting order
```

### 12.5 Resting Order Partially Filled

When a resting order is partially filled:

```text
TradeExecuted
OrderPartiallyFilled for resting order
```

### 12.6 Rejected Order

If a new order is invalid:

```text
OrderRejected
```

The rejection event should include a deterministic rejection reason.

---

## 13. Trade Event Semantics

Each trade event must identify:

```text
instrument id
trade id
incoming order id
resting order id
aggressor side
execution price
executed quantity
command sequence
event sequence
```

Trade ID generation must be deterministic.

For the prototype:

```text
trade_id = monotonically increasing per instrument
```

or:

```text
trade_id = global monotonically increasing event-derived sequence
```

Choose one and keep it stable.

Recommended for first prototype:

```text
trade_id is monotonically increasing per instrument
```

---

## 14. Cancel Order Processing

Cancel command processing:

```text
1. Validate command.
2. If order does not exist, emit OrderRejected.
3. If order exists:
   - remove order from active order index;
   - remove order from its price level FIFO queue;
   - remove price level if empty;
   - emit OrderCancelled.
4. Verify invariants.
```

A cancelled order must not be matched later.

Cancellation does not generate a trade.

Current prototype status:

```text
Implemented:
- existing order cancellation
- unknown order rejection with UnknownOrderId
- instrument mismatch rejection with InstrumentMismatch
- FIFO price-level removal by linear scan
- empty price-level removal
- replay coverage for cancel event streams

Not implemented:
- ownership/client mismatch checks
```

---

## 15. Replace Order Processing

Replacement is more subtle than cancellation.

For the first prototype, implement replace as:

```text
Cancel old order + New order
```

This means:

```text
replacement loses FIFO priority
```

Replacement processing:

```text
1. Validate replace command.
2. If invalid, emit OrderRejected.
3. Remove old order from the book.
4. Emit OrderCancelled for the old order.
5. Process replacement_order_id as a new order with new price and quantity.
6. New order receives new FIFO priority.
7. Verify invariants.
```

Do not attempt priority-preserving replace in the first prototype.

Current prototype status:

```text
Implemented:
- ReplaceOrder with distinct replacement_order_id
- unknown old order rejection with UnknownOrderId
- duplicate replacement id rejection with ReplaceWouldDuplicateOrderId
- invalid replacement id rejection with InvalidReplacementOrderId
- instrument mismatch rejection with InstrumentMismatch
- old order removal from active index and FIFO price level
- OrderCancelled event for the old order
- replacement application as fresh NewOrder with new FIFO priority
- replay coverage for replace event streams

Not implemented:
- ownership/client mismatch checks
- priority-preserving replace
```

---

## 16. Partial Fill Rules

If incoming quantity is greater than resting quantity:

```text
resting order fully filled
incoming order remains active for further matching
```

If incoming quantity is less than resting quantity:

```text
resting order remains in book with reduced quantity
incoming order fully filled
```

If quantities are equal:

```text
both orders are fully filled
resting order is removed
incoming order does not rest
```

All cases must emit deterministic events.

---

## 17. Book Insertion Rules

A new order is inserted into the book only if:

```text
it has remaining quantity after matching
time in force allows resting
it passed validation
```

Insertion steps:

```text
1. Find or create price level.
2. Append order to the end of the FIFO queue.
3. Add order to active order index.
4. Update price level aggregate quantity if maintained.
```

The inserted order receives the latest FIFO position at its price level.

---

## 18. Time-In-Force Rules

The first implementation supports only GTC.

## 18.1 GTC

Good Till Cancelled orders may rest in the book.

If not fully executed immediately, the remaining quantity is inserted into the book.

## 18.2 IOC

Optional later feature.

Immediate Or Cancel orders may execute immediately but must not rest.

If quantity remains after matching, the remainder is cancelled.

## 18.3 FOK

Optional later feature.

Fill Or Kill orders must be fully executable immediately or not execute at all.

FOK requires pre-checking available quantity across crossing price levels.

Do not implement FOK until the basic deterministic matching path is stable.

---

## 19. Order Book Invariants

After every command, the following invariants must hold:

```text
No active order has zero or negative remaining quantity.
No active order has invalid side.
No active order has invalid price.
No active order has invalid quantity.
Order IDs are unique among active orders.
Every active order exists in exactly one price level.
Every order in a price level exists in the active order index.
Every order in the active order index exists in exactly one price level.
Empty price levels are removed.
Bid price levels are sorted descending.
Ask price levels are sorted ascending.
FIFO order is preserved inside every price level.
Best bid is lower than best ask after matching completes.
```

These invariants should be checked in tests.

Debug builds may also check them after every command.

---

## 20. Determinism Rules

The matcher must be deterministic.

It must not use:

```text
wall-clock time
random numbers
unordered container iteration where order affects output
thread scheduling
external services
database state
network state
mutable global state
```

If a timestamp is required, it must be part of the input command.

If a sequence number is required, it must be assigned by the log or deterministic sequencer.

If an unordered container is used for lookup, it must not affect event order.

---

## 21. Rejection Reasons

Rejection reasons must be explicit and deterministic.

Possible rejection reasons:

```text
UnknownInstrument
InvalidCommandType
InvalidSide
InvalidPrice
InvalidQuantity
DuplicateOrderId
UnknownOrderId
UnsupportedTimeInForce
ClientMismatch
InstrumentMismatch
ReplaceWouldDuplicateOrderId
InternalError
```

Rejection reason values should be stable because they are part of replayable event output.

---

## 22. Examples

## 22.1 Passive Buy Order

Initial book:

```text
empty
```

Command:

```text
NewOrder Buy 100.00 qty 10 order B1
```

Events:

```text
OrderAccepted B1 qty 10 price 100.00
```

Final book:

```text
Bids:
100.00: B1 qty 10

Asks:
empty
```

## 22.2 Passive Sell Order

Initial book:

```text
Bids:
100.00: B1 qty 10
```

Command:

```text
NewOrder Sell 101.00 qty 5 order S1
```

Events:

```text
OrderAccepted S1 qty 5 price 101.00
```

Final book:

```text
Bids:
100.00: B1 qty 10

Asks:
101.00: S1 qty 5
```

## 22.3 Aggressive Buy Fully Fills Resting Sell

Initial book:

```text
Asks:
101.00: S1 qty 5
```

Command:

```text
NewOrder Buy 101.00 qty 5 order B1
```

Events:

```text
OrderAccepted B1 qty 5 price 101.00
TradeExecuted trade 1 incoming B1 resting S1 price 101.00 qty 5
OrderFullyFilled S1
OrderFullyFilled B1
```

Final book:

```text
empty
```

## 22.4 Aggressive Buy Partially Fills Resting Sell

Initial book:

```text
Asks:
101.00: S1 qty 10
```

Command:

```text
NewOrder Buy 101.00 qty 4 order B1
```

Events:

```text
OrderAccepted B1 qty 4 price 101.00
TradeExecuted trade 1 incoming B1 resting S1 price 101.00 qty 4
OrderPartiallyFilled S1 remaining 6
OrderFullyFilled B1
```

Final book:

```text
Asks:
101.00: S1 qty 6
```

## 22.5 Aggressive Buy Sweeps Multiple Price Levels

Initial book:

```text
Asks:
101.00: S1 qty 5
101.25: S2 qty 7
101.50: S3 qty 10
```

Command:

```text
NewOrder Buy 101.50 qty 15 order B1
```

Events:

```text
OrderAccepted B1 qty 15 price 101.50
TradeExecuted trade 1 incoming B1 resting S1 price 101.00 qty 5
OrderFullyFilled S1
TradeExecuted trade 2 incoming B1 resting S2 price 101.25 qty 7
OrderFullyFilled S2
TradeExecuted trade 3 incoming B1 resting S3 price 101.50 qty 3
OrderPartiallyFilled S3 remaining 7
OrderFullyFilled B1
```

Final book:

```text
Asks:
101.50: S3 qty 7
```

## 22.6 Aggressive Buy Partially Fills and Rests

Initial book:

```text
Asks:
101.00: S1 qty 5
```

Command:

```text
NewOrder Buy 101.00 qty 10 order B1
```

Events:

```text
OrderAccepted B1 qty 10 price 101.00
TradeExecuted trade 1 incoming B1 resting S1 price 101.00 qty 5
OrderFullyFilled S1
OrderPartiallyFilled B1 remaining 5
OrderRested B1 remaining 5 price 101.00
```

Final book:

```text
Bids:
101.00: B1 qty 5
```

If `OrderRested` is not implemented, the final event must still make the resting remainder explicit.

## 22.7 Cancel Existing Order

Initial book:

```text
Bids:
100.00: B1 qty 10
```

Command:

```text
CancelOrder B1
```

Events:

```text
OrderCancelled B1
```

Final book:

```text
empty
```

## 22.8 Cancel Unknown Order

Initial book:

```text
empty
```

Command:

```text
CancelOrder B1
```

Events:

```text
OrderRejected B1 reason UnknownOrderId
```

Final book:

```text
empty
```

---

## 23. Minimal Acceptance Criteria

The matching rules implementation is acceptable when:

```text
Passive buy orders rest in the bid book.
Passive sell orders rest in the ask book.
Aggressive buy orders match best asks first.
Aggressive sell orders match best bids first.
FIFO priority is preserved within a price level.
Partial fills are handled correctly.
Full fills remove orders from the book.
Cancelled orders are removed from the book.
Unknown cancels are rejected deterministically.
Duplicate order ids are rejected.
Invalid price and quantity are rejected.
Execution prices are resting order prices.
The same command sequence always produces the same event sequence.
Order book invariants hold after each command.
```

---

## 24. Summary

The first prototype implements a small deterministic continuous limit order book.

The essential rules are:

```text
Best price first.
FIFO inside price level.
Execution at resting order price.
One command applied at a time.
No non-deterministic state inside the matcher.
Every state transition produces deterministic events.
```

The rule set is intentionally limited.

A small correct deterministic matcher is more valuable than a large incomplete trading simulator.
