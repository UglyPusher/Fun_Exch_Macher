# Current Architecture

This document describes what is implemented in the current codebase. For the intended larger design, see `expected_architecture.md`.

## Current Implemented Flow

```text
[Domain Storage DTOs]
        |
        v
[Typed WAL Adapter]
        |
        v
[Raw WAL API]
        |
        v
[Binary Segment Writer / Reader / Scanner]
        |
        v
[Pending Positions]
        |
        v
[Commit / Flush Ack]
        |
        v
[Committed Position Queue]
        |
        v
[Committed Command Reader]
        |
        v
[DummyInstrumentEngine]
        |
        v
[Execution Event WAL]
```

The current prototype is centered on the WAL integrity boundary and fixed-layout domain storage records. It now has a dummy engine pipeline that proves matcher I/O, but it does not yet run a real order book or matcher.

The first in-memory `OrderBook` FSM also exists for `NewOrder`, but it is not wired into the WAL pipeline yet.

## Implemented Now

```text
src/wal/
  - WalPosition: stream_id + epoch + sequence
  - fixed segment header
  - fixed record header
  - payload CRC32
  - record/header validation
  - binary segment writer
  - binary segment reader
  - segment scanner for recovery decisions
  - raw WAL reader/writer interfaces
  - typed WAL reader/writer adapters
  - pending-to-committed writer boundary
  - committed position queue after write/flush acknowledgement

src/domain/
  - OrderCommandRecordV1
  - ExecutionEventRecordV1
  - RecordType enum

src/core/
  - DummyInstrumentEngine
  - command-to-OrderAccepted event conversion
  - in-memory OrderBook FSM for NewOrder
  - passive resting
  - price/time matching against resting liquidity
  - partial fill and full fill event generation
  - duplicate order rejection
  - no Cancel/Replace support yet
  - no real InstrumentEngine wiring yet

src/app/
  - placeholder CLI entrypoint

tests/
  - WAL behavior tests
  - in-memory OrderBook NewOrder tests
  - dummy Command WAL -> engine -> Event WAL pipeline test
  - placeholder smoke tests for matching and replay
```

## Current Build Shape

```text
matching_engine_domain
        |
        v
matching_engine_core ---------------> matching_engine_wal
        |
        v
matching_engine_app
```

`matching_engine_core` is now a small library. It links the domain DTO layer and WAL library, and contains the dummy instrument engine used to verify matcher I/O before the real instrument engine is wired in.
It also contains the first in-memory `OrderBook` implementation for `NewOrder`.

## Current Dummy Pipeline

```text
OrderCommandRecordV1
    -> Command WAL append
    -> Command WAL commit ack
    -> committed command read through typed DTO boundary
    -> DummyInstrumentEngine
    -> ExecutionEventRecordV1(OrderAccepted)
    -> Event WAL append
    -> Event WAL commit ack
    -> committed event read through typed DTO boundary
```

The dummy engine intentionally does not match orders. It copies command identity and order fields into an `OrderAccepted` event so the project can verify:

```text
- matcher input boundary
- matcher output boundary
- commit visibility
- typed DTO serialization/deserialization
- command_sequence correlation
```

## Current OrderBook Scope

```text
OrderBook.apply_new_order(command)
    -> validates NewOrder
    -> emits deterministic events
    -> updates in-memory bid/ask state
```

Implemented `NewOrder` event sequences:

```text
Rejected:
  OrderRejected

Passive:
  OrderAccepted
  OrderRested

Aggressive full fill:
  OrderAccepted
  TradeExecuted...
  OrderFullyFilled

Aggressive partial fill with resting remainder:
  OrderAccepted
  TradeExecuted...
  OrderPartiallyFilled
```

The current `OrderBook` is deliberately pure in-memory code. It does not know about WAL files, disk writers, command readers, or event writers.

## Current WAL Shape

```text
Domain DTO
    |
    v
TypedWalWriter / TypedWalReader
    |
    v
RawWalWriter / RawWalReader
    |
    v
WalSegmentWriter / WalSegmentReader / WalSegmentScanner
    |
    v
Binary segment file
```

Current WAL properties:

```text
- binary segment format
- payload-agnostic raw records
- stream-local record identity
- segment header validation
- record header validation
- payload checksum validation
- sequence validation
- typed DTO boundary validation
- records become visible through committed positions after commit
- recovery scanner for incomplete trailing records
```

## Not Implemented Yet

```text
- ingress normalizer/router
- per-instrument command stream ownership
- risk checks
- reservation manager
- real matcher command application
- Cancel/Replace support
- WAL pipeline integration for the real InstrumentEngine
- deterministic replay harness comparing generated events with stored event WAL
- portfolio state projection
- accounting projection
- market data projection
- snapshots
- persisted committed-offset metadata across process restarts
- POSIX fsync/fdatasync durability policy
- multi-segment rotation
```

## Current Source Of Truth

At the current stage, the reliable implemented source-of-truth mechanism is the WAL storage layer itself:

```text
Binary segment file
    -> Raw WAL validation
    -> Typed DTO boundary
```

At the architecture level, the intended truth model is still:

```text
Command WAL + Matching Rules = Execution Event WAL
```

The dummy pipeline exercises the I/O shape of this invariant. Full deterministic validation still waits for real matching rules and replay comparison.
