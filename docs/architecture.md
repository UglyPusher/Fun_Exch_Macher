# Architecture

## 1. Purpose

This project is a minimal deterministic matching engine prototype.

The goal is not to implement a complete exchange platform. The goal is to demonstrate a clean architecture for replayable order processing, deterministic matching, append-only event logging, and explicit component boundaries.

The project focuses on the core trading path:

```text
Normalized Command Log -> Deterministic Matcher -> Execution Event Log
```

The system is intentionally scoped down. It does not include authentication, FIX connectivity, market data distribution, risk checks, portfolio accounting, database mirroring, clustering, or failover.

Those components are important in a real exchange system, but they are outside the first prototype.

---

## 2. Design Goals

The main design goals are:

* deterministic order processing;
* explicit ordering of commands;
* append-only persistence model;
* replayability from logs;
* simple and testable order book state machine;
* strict separation between protocol handling and matching logic;
* one active matcher per instrument;
* minimal shared mutable state;
* clear distinction between commands and resulting execution events.

The architecture is optimized for correctness, auditability, and explainability before raw performance.

---

## 3. Non-Goals

The first version does not attempt to solve:

* external client authentication;
* session management;
* FIX protocol;
* binary market data feed;
* network transport;
* database schema;
* risk engine;
* portfolio management;
* high availability;
* distributed consensus;
* cross-instrument order types;
* persistence optimization;
* production-grade latency tuning.

The matching core must remain small enough to reason about directly.

---

## 4. High-Level Data Flow

```text
Client / Test Driver
        |
        v
Trading Ingress
        |
        v
Normalizer / Validator
        |
        v
Command WAL
        |
        v
Instrument Matcher
        |
        v
Order Book State Machine
        |
        v
Execution Event WAL
        |
        v
Replay / Tests / Consumers
```

In the first prototype, `Client / Test Driver`, `Trading Ingress`, and `Normalizer / Validator` may be represented by a simple input generator or command loader.

The matcher receives already normalized commands. It does not parse external protocols and does not depend on network I/O.

---

## 5. Core Principle

The system treats logs as the source of truth.

The command log records accepted normalized input commands. The execution event log records the deterministic result of processing those commands.

```text
Command WAL + Matching Rules = Execution Event WAL
```

Given the same command sequence and the same matching rules, the engine must produce the same sequence of execution events.

This is the central correctness property of the prototype.

---

## 6. Component Overview

### 6.1 Trading Ingress

The trading ingress is responsible for receiving external requests.

In the prototype this may be replaced by a test driver.

In a real system, this component would handle:

* network protocol;
* authentication;
* session state;
* rate limits;
* preliminary validation;
* conversion from external messages to internal commands.

The matcher must not know anything about these details.

### 6.2 Normalizer / Validator

The normalizer converts external requests into internal command DTOs.

Its responsibilities are:

* validate basic command structure;
* resolve instrument identifiers;
* normalize side, price, quantity, order identifiers;
* assign or preserve command sequence metadata;
* write normalized commands to the command log.

The output of this component is a normalized command suitable for deterministic processing.

### 6.3 Command WAL

The command WAL is an append-only log of normalized commands.

It provides:

* stable command ordering;
* recovery input;
* audit trail;
* deterministic replay source.

For the first prototype, this can be implemented as a simple file-based log. The first format may be text-based or binary-lite, as long as record order and payload integrity are explicit.

A later version may introduce:

* fixed binary record layout;
* record length;
* record type;
* sequence number;
* payload size;
* checksum;
* alignment.

### 6.4 Instrument Matcher

The instrument matcher is the active component that processes commands for a single instrument.

It owns:

* the order book state;
* the matching loop;
* command application logic;
* execution event generation.

The matcher is single-threaded per instrument.

This is a deliberate design choice. It provides:

* deterministic command order;
* no internal lock contention;
* simpler reasoning;
* simpler replay;
* simpler unit testing.

Scaling is achieved by assigning different instruments to different matcher workers, not by processing one order book concurrently from multiple threads.

### 6.5 Order Book State Machine

The order book is a deterministic state machine.

Input:

```text
OrderCommand
```

State:

```text
Bid side
Ask side
Active order index
Price levels
FIFO queues inside price levels
```

Output:

```text
ExecutionEvent[]
```

The order book must not perform I/O. It must not call external services. It must not read wall-clock time as part of deterministic matching behavior.

### 6.6 Execution Event WAL

The execution event WAL is an append-only log of the result of command processing.

It records events such as:

* order accepted;
* order rejected;
* order cancelled;
* order replaced;
* trade executed;
* order partially filled;
* order fully filled.

This log is the downstream source for:

* market data;
* reports;
* database mirrors;
* audit tools;
* replay validation;
* tests.

---

## 7. Command Model

A command represents an intention submitted to the trading system after normalization.

The minimal command set is:

```cpp
enum class CommandType
{
    NewOrder,
    CancelOrder,
    ReplaceOrder
};
```

A simplified command DTO:

```cpp
struct OrderCommand
{
    uint64_t command_sequence;
    uint64_t source_record_id;

    InstrumentId instrument_id;
    CommandType command_type;

    ClientId client_id;
    OrderId order_id;

    Side side;
    Price price;
    Quantity quantity;

    TimeInForce time_in_force;
};
```

The exact C++ type definitions are implementation details. The architectural requirement is that commands are explicit, normalized, and replayable.

The matcher must never parse external request formats.

---

## 8. Event Model

An execution event describes what happened as a result of applying a command.

The minimal event set is:

```cpp
enum class ExecutionEventType
{
    OrderAccepted,
    OrderRejected,
    OrderCancelled,
    OrderReplaced,
    TradeExecuted,
    OrderPartiallyFilled,
    OrderFullyFilled
};
```

A simplified execution event DTO:

```cpp
struct ExecutionEvent
{
    uint64_t event_sequence;
    uint64_t command_sequence;

    InstrumentId instrument_id;
    ExecutionEventType event_type;

    OrderId order_id;
    OrderId contra_order_id;

    Price price;
    Quantity quantity;

    TradeId trade_id;
};
```

A single command may produce zero, one, or multiple execution events.

For example, a new aggressive buy order may execute against several resting sell orders and then either be fully filled or rest in the book.

---

## 9. Matching Model

The first prototype supports limit orders.

A buy order crosses the book when:

```text
buy.price >= best_ask.price
```

A sell order crosses the book when:

```text
sell.price <= best_bid.price
```

The priority model is:

```text
price priority first
FIFO priority inside each price level
```

Basic processing algorithm:

```text
1. Read the next command.
2. Validate command against the current book state.
3. If the command is invalid, emit rejection event.
4. If the command is a new order:
   1. Check whether it crosses the opposite side.
   2. While it crosses and remaining quantity is positive:
      - take the best opposite price level;
      - take the oldest resting order at that level;
      - execute min(incoming quantity, resting quantity);
      - emit trade event;
      - update both quantities;
      - remove fully filled resting orders;
      - remove empty price levels.
   3. If remaining quantity is positive and the order can rest:
      - insert the order into the book;
      - emit accepted or partially filled event.
   4. If no quantity remains:
      - emit fully filled event if appropriate.
5. Append generated events to the execution event log.
```

Exact event sequencing rules must be documented and tested.

---

## 10. Order Book Invariants

The order book must preserve the following invariants after every command is applied:

```text
No active order has zero or negative quantity.
Order IDs are unique among active orders.
Empty price levels are removed.
FIFO order is preserved inside each price level.
Best bid is lower than best ask after matching completes.
Total quantity per price level is non-negative.
A fully filled order is not present in the active order index.
A cancelled order is not present in the active order index.
```

These invariants must be covered by unit tests.

---

## 11. Replay

Replay is a core architectural feature.

Replay procedure:

```text
1. Create an empty order book.
2. Read the command WAL from the beginning or from a checkpoint.
3. Apply commands in the original order.
4. Generate execution events again.
5. Compare generated events with the stored execution event WAL.
```

Replay correctness criterion:

```text
same command sequence + same matching rules = same execution event sequence
```

If replay produces a different event sequence, one of the following is true:

* the command log is corrupted;
* the event log is corrupted;
* matching logic changed;
* non-deterministic behavior leaked into the matcher;
* replay started from an invalid checkpoint;
* the event comparison rules are incomplete.

Replay is not an optional debug feature. It is part of the core system model.

---

## 12. Checkpoints

The first prototype may replay from the beginning of the command log.

A later version may introduce checkpoints.

A checkpoint contains enough state to restore an order book without replaying the whole command history.

A checkpoint must be associated with:

```text
instrument_id
last_applied_command_sequence
order book snapshot
checksum or validation metadata
```

After loading a checkpoint, replay continues from the next command sequence.

Checkpoints are an optimization. They must not replace the command log as the source of truth.

---

## 13. Concurrency Model

The first prototype uses a single matcher for one instrument.

The next step may support multiple instruments with one matcher worker per instrument or per instrument group.

```text
Instrument A -> Matcher Worker 1
Instrument B -> Matcher Worker 2
Instrument C -> Matcher Worker 2
Instrument D -> Matcher Worker 3
```

The key rule:

```text
Only one active matcher may mutate a given instrument order book.
```

This avoids lock contention inside the book and preserves deterministic command order.

Cross-instrument operations are outside the prototype scope.

---

## 14. Fault Boundaries

The matcher must not depend on components that can introduce non-determinism or uncontrolled latency.

The matcher must not:

* parse network protocols;
* perform blocking network I/O;
* call databases;
* allocate heavily on the hot path without control;
* use wall-clock time for matching decisions;
* call external services;
* depend on mutable global state.

The matcher may:

* read normalized commands;
* mutate its local order book;
* generate execution events;
* append events to the output log through a narrow interface.

---

## 15. Persistence Boundaries

The first prototype separates two persistence concerns:

```text
Command persistence
Execution event persistence
```

Command persistence records what the system accepted for processing.

Execution event persistence records what the matcher produced.

This distinction matters because input commands and output events have different semantics.

Commands are requests.

Events are facts produced by deterministic processing.

---

## 16. Testing Strategy

The core must be testable without network, database, or external services.

Required test groups:

```text
Order book insertion tests
Price priority tests
FIFO priority tests
Partial fill tests
Full fill tests
Cancel tests
Reject tests
Replay tests
Event sequence tests
Invariant tests
```

Replay tests are especially important.

A test should be able to:

```text
1. Feed a known command sequence.
2. Capture generated events.
3. Rebuild the book from the command sequence.
4. Generate events again.
5. Verify that the event sequence is identical.
```

---

## 17. Prototype Scope

### Must Have

```text
One instrument
Limit buy orders
Limit sell orders
Price/time priority
Command log
Execution event log
Replay test
Deterministic unit tests
```

### Should Have

```text
Cancel order
Multiple instruments
One matcher per instrument
Simple benchmark
Snapshot/checkpoint support
```

### Not in First Prototype

```text
FIX
Authentication
Risk engine
Market data protocol
Database persistence
Cluster failover
Recovery quorum
Advanced order types
Iceberg orders
Stop orders
Cross-instrument matching
```

---

## 18. Suggested Project Structure

```text
matching_engine/
├── README.md
├── docs/
│   ├── architecture.md
│   ├── matching_rules.md
│   ├── wal.md
│   └── replay.md
├── src/
│   ├── app/
│   ├── domain/
│   ├── matcher/
│   ├── order_book/
│   └── wal/
├── tests/
│   ├── matching_tests.cpp
│   ├── order_book_tests.cpp
│   ├── replay_tests.cpp
│   └── invariant_tests.cpp
└── examples/
    └── simple_session.txt
```

---

## 19. Architectural Trade-Offs

### Single-threaded matcher per instrument

This reduces internal concurrency complexity and improves determinism.

The trade-off is that one very active instrument is limited by one matcher worker.

For this prototype, correctness and clarity are more important than maximum throughput.

### WAL-first design

The WAL-first approach improves auditability and replayability.

The trade-off is additional persistence design complexity.

For the prototype, a simple file-backed log is acceptable.

### No external protocol in matcher

This keeps the matching core isolated and testable.

The trade-off is the need for a separate normalizer layer.

This is intentional. Protocol parsing and order book mutation belong to different components.

---

## 20. Interview-Level Summary

This prototype demonstrates a deterministic exchange-core design.

The important architectural choices are:

```text
Append-only logs as source of truth.
Normalized command DTOs before matching.
Single-threaded matcher per instrument.
Order book as deterministic state machine.
Execution events as durable output facts.
Replay as a correctness mechanism.
Strict boundary between protocol handling and matching logic.
```

The project is intentionally small.

It is not intended to prove that a complete exchange can be built quickly. It is intended to show that the core trading path can be modelled with explicit ordering, deterministic state transitions, replayable logs, and clean component boundaries.
