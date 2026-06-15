# Matching Engine

A small C++20 deterministic matching-engine prototype built around explicit WAL boundaries.

This is not a production exchange and not an HFT system. The goal is to demonstrate a clean trading-core architecture: deterministic command processing, append-only facts, replayability, storage DTO boundaries, and a small codebase that is easy to reason about.

```text
Normalized Command Log -> Deterministic Matcher -> Execution Event Log
```

## Current Status

Implemented now:

- CMake-native build, test, run, and debug workflows via `CMakePresets.json`.
- WAL core types: positions, record headers, record views, result/status types, and errors.
- Raw WAL interfaces: `RawWalWriter` and `RawWalReader`.
- Typed WAL adapters for trivially-copyable storage DTOs.
- Binary segment writer/reader/scanner with segment headers, record headers, payload CRC32, sequence validation, and trailing-record detection.
- Domain storage DTOs for order commands and execution events.
- WAL-focused tests covering headers, checksum stability, segment write/read, recovery scanning, and typed adapters.
- A tiny CLI entrypoint that loads `examples/simple_session.txt` as a placeholder app target.

Not implemented yet:

- Actual order book FSM.
- Matcher command application.
- Execution event generation from matching rules.
- Deterministic replay harness comparing generated events with stored event WAL.
- Ingress normalizer/router and per-instrument command streams.

## Architecture Direction

The intended production-shaped flow is:

```text
External Input
    -> Trading Ingress WAL
    -> Normalizer / Router
    -> CommandStream / InstrumentCommandWAL
    -> Single-threaded Matcher per Instrument
    -> OrderBook FSM
    -> ExecutionEventWAL
    -> Replay / Consumers / DB mirror / Market data
```

The core invariant is:

```text
same command sequence + same matching rules = same execution event sequence
```

WAL records are identified by stream-local position:

```cpp
struct WalPosition
{
    StreamId stream_id;
    EpochId epoch;
    SequenceNumber sequence;
};
```

There is deliberately no single global sequence for everything. Ingress, command streams, execution event streams, and trade IDs are separate sequences.

## Layout

```text
docs/                 Architecture and behavior notes
examples/             Sample input sessions
src/app/              CLI entrypoint
src/domain/           Storage DTOs and domain record types
src/wal/              WAL core, typed adapters, and segment storage
tests/                Smoke and WAL behavior tests
```

Detailed design notes live in:

- `docs/architecture.md`
- `docs/wal.md`
- `docs/matching_rules.md`
- `docs/replay.md`

## Requirements

- CMake 3.25+ for workflow presets.
- C++20 compiler.
- Linux-like environment.
- Optional: `gdb` for the debug target.

## Commands

From the repository root `matching_engine/`:

```bash
cmake --workflow --preset build
```

Configure and build the debug preset.

```bash
cmake --workflow --preset test
```

Configure, build, and run the test suite.

```bash
cmake --workflow --preset run
```

Build and run the placeholder app against `examples/simple_session.txt`.

```bash
cmake --workflow --preset debug
```

Build and start the app under `gdb`.

Lower-level commands are also available:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cmake --build --preset run
cmake --build --preset debug-run
```

Release build:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

## WAL Design Snapshot

The WAL core is payload-agnostic. It knows only:

- record header;
- record type;
- stream id;
- epoch;
- sequence;
- payload length;
- payload bytes;
- checksum;
- alignment;
- commit result.

Domain decoding lives above the raw WAL layer:

```text
Raw WAL -> Typed adapter -> Domain storage DTO
```

The typed adapter uses `std::as_bytes` and `std::memcpy`, so records must be fixed-layout, trivially copyable storage DTOs. They must not contain `std::string`, `std::vector`, pointers, allocator state, or business-object behavior.

## Testing

Run everything with:

```bash
cmake --workflow --preset test
```

Current test groups include:

- WAL record header validation.
- CRC32 stability and payload-change detection.
- Segment writer append and sequence increment.
- Segment reader ordered read and EOF detection.
- Recovery scanner detection of incomplete trailing records.
- Typed WAL write/read, record type mismatch, and payload size mismatch.

## Scope Guard

Out of scope for the first prototype:

- FIX/session management/authentication.
- Market data protocol.
- Database persistence.
- Risk engine and portfolio accounting.
- Clustering, failover, consensus, and replicated durability.
- Advanced order types.

First meaningful prototype target:

- one instrument;
- limit buy/sell orders;
- price/time priority;
- command WAL;
- execution event WAL;
- deterministic replay test;
- raw and typed WAL adapters;
- simple file segment writer/reader.

## Interview Positioning

This is a small deterministic trading-core prototype showing explicit ordering, append-only logs, replay, order book invariants, and clean boundaries between ingress, normalization, matching, and durability infrastructure.
