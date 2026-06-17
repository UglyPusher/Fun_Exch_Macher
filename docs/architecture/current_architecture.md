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
```

The current prototype is centered on the WAL integrity boundary and fixed-layout domain storage records. It does not yet run a real order book or matcher.

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

src/app/
  - placeholder CLI entrypoint

tests/
  - WAL behavior tests
  - placeholder smoke tests for order book, matching, and replay
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

`matching_engine_core` is currently an interface target that links the domain DTO layer and WAL library. The actual matching core is not implemented yet.

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
- actual order book FSM
- matcher command application
- execution event generation from matching rules
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

However, the matcher and execution-event generation pieces are not present yet, so this invariant is documented but not fully executable today.
