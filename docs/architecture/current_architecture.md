# Current Architecture

This document describes what is implemented in the current codebase. For the intended larger design, see `expected_architecture.md`.

## Current Implemented Flow

```text
[OrderCommandRecordV1]
        |
        v
[Typed Command WAL Writer]
        |
        v
[Binary Command WAL Segment]
        |
        v
[Commit / Flush Ack]
        |
        v
[Committed Command Reader]
        |
        v
[InstrumentEngine]
        |
        v
[OrderBook NewOrder FSM]
        |
        v
[Typed Event WAL Writer]
        |
        v
[Binary Execution Event WAL Segment]
        |
        v
[Committed Event Reader]
```

The current prototype now has the first real matcher I/O cycle for `NewOrder`:

```text
Command WAL -> InstrumentEngine -> OrderBook mutation -> Execution Event WAL
```

This is still intentionally narrow. It proves the WAL boundaries, typed DTO boundaries, commit visibility, command-to-event correlation, and in-memory book mutation for `NewOrder` only.

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
  - matching enum constants for command, side, TIF, event type, rejection reason
  - DummyInstrumentEngine retained as an earlier scaffold
  - InstrumentEngine for NewOrder
  - InstrumentEngine owns an in-memory OrderBook
  - OrderBook FSM for NewOrder
  - passive resting
  - price/time matching against resting liquidity
  - partial fill and full fill event generation
  - duplicate order rejection
  - no Cancel/Replace support yet

src/app/
  - placeholder CLI entrypoint

tests/
  - WAL behavior tests
  - in-memory OrderBook NewOrder tests
  - Command WAL -> InstrumentEngine -> Event WAL pipeline test
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

`matching_engine_core` is now a small library. It links the domain DTO layer and WAL library, contains the `InstrumentEngine`, and owns the first in-memory `OrderBook` implementation for `NewOrder`.

## Current Instrument Pipeline

```text
OrderCommandRecordV1[]
    -> Command WAL append
    -> Command WAL commit ack
    -> committed command read through typed DTO boundary
    -> InstrumentEngine.apply(command)
    -> OrderBook.apply_new_order(command)
    -> ExecutionEventRecordV1[]
    -> Event WAL append
    -> Event WAL commit ack
    -> committed event read through typed DTO boundary
```

The pipeline test covers:

```text
- passive buy
- passive sell
- aggressive full fill
- partial fill with resting remainder
- duplicate order rejection
- event sequence continuity
- command_sequence correlation
- WAL reader CRC/header/sequence validation on readback
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

The current `OrderBook` remains pure in-memory code. It does not know about WAL files, disk writers, command readers, or event writers.

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
- Cancel/Replace support
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

At the current stage, the reliable implemented source-of-truth mechanism is the WAL storage layer plus deterministic `NewOrder` matching rules:

```text
Command WAL + NewOrder Matching Rules = Execution Event WAL
```

Full historical replay comparison is not implemented yet, but the current pipeline already regenerates and stores execution events from committed command input.
