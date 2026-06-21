# Matching Engine

A small C++20 deterministic matching-engine prototype built around explicit WAL boundaries.

This is not a production exchange and not an HFT system. The goal is to demonstrate a clean trading-core architecture: deterministic command processing, append-only facts, replayability, storage DTO boundaries, and a small codebase that is easy to reason about.

```text
Normalized Command Log -> Deterministic Matcher -> Execution Event Log
```

The current project is still a prototype. In particular, WAL `commit()` currently
flushes the C++ stream buffer; it is not an `fsync` / `fdatasync` durable
boundary yet.

## Current Status

Implemented now:

- CMake-native build, test, run, and debug workflows via `CMakePresets.json`.
- WAL core types: positions, record headers, record views, result/status types, and errors.
- Raw WAL interfaces: `RawWalWriter` and `RawWalReader`.
- Typed WAL adapters for trivially-copyable storage DTOs.
- Binary segment writer/reader/scanner with segment headers, record headers, payload CRC32, sequence validation, and trailing-record detection.
- Domain storage DTOs for order commands and execution events.
- In-memory `OrderBook` FSM for `NewOrder`: passive resting, price/time matching, partial fill, full fill, and duplicate rejection.
- In-memory `OrderBook` support for `CancelOrder`: existing-order cancellation, unknown-order rejection, instrument mismatch rejection, FIFO price-level removal, and invariant validation.
- In-memory `OrderBook` support for `ReplaceOrder` with a distinct `replacement_order_id`; replacement removes the old order and processes the replacement as a new order with fresh FIFO priority.
- `InstrumentEngine` for `NewOrder`, `CancelOrder`, and `ReplaceOrder` that owns an in-memory `OrderBook`.
- Deterministic replay validation harness comparing regenerated execution events with stored execution events through normalized event fields.
- Market data projection from Execution Event WAL into public book depth and trade tape.
- End-to-end instrument pipeline test covering Command WAL -> committed command reader -> InstrumentEngine -> Event WAL -> event reader.
- WAL-focused tests covering headers, checksum stability, segment write/read, recovery scanning, and typed adapters.
- Replay tests covering happy paths, event count mismatch, event field mismatch, event order mismatch, extra/missing stored events, command sequence breaks, cancel replay, and replace replay.
- CLI/demo runner with `run`, `replay`, `dump-events`, `dump-book`, and `dump-trades` commands.
- Manual load benchmark for separating command WAL write, command WAL read, matcher-only, Event WAL append, and full read/match/event pipeline costs.
- Local README files for each meaningful `src/` directory, including reserved scaffold directories.

Not implemented yet:

- Ingress normalizer/router and per-instrument command streams.
- Self-trade prevention.
- Accounts, balances, reservation, risk checks, portfolio, accounting, futures, margin, liquidation, snapshots, fsync/fdatasync durability policy, batch commit recovery contract, and multi-segment rotation.

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
benchmarks/           Manual throughput tools and local benchmark results
docs/                 Architecture and behavior notes
examples/             Sample input sessions
src/app/              CLI entrypoint
src/core/             Instrument engine, order book, and matching core boundary
src/domain/           Storage DTOs and domain record types
src/matcher/          Reserved scaffold; active matching code is not here yet
src/order_book/       Reserved scaffold; active order book lives in src/core
src/projections/      Downstream projections from execution events
src/wal/              WAL core, typed adapters, and segment storage
tests/                Smoke and WAL behavior tests
TODO.md               Future architecture work that is not part of this stage
```

Detailed design notes live in:

- `docs/CURRENT_STATE_DOCUMENTATION.md`
- `docs/architecture.md`
- `docs/architecture/current_architecture.md`
- `docs/architecture/expected_architecture.md`
- `docs/wal.md`
- `docs/matching_rules.md`
- `docs/replay.md`
- `src/README.md`
- `benchmarks/README.md`
- `benchmarks/results/README.md`
- `TODO.md`

Active matching code currently lives in `src/core/`. The `src/matcher/` and
`src/order_book/` directories are reserved for possible future extraction and
must not receive duplicate implementations casually.

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

Build and run the demo session against `examples/simple_session.txt`.

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

Run a scenario through Command WAL, `InstrumentEngine`, Execution Event WAL, and validation replay:

```bash
./build/debug/matching_engine run examples/cancel_replace_session.txt wal/commands.wal wal/events.wal
```

Replay existing WALs:

```bash
./build/debug/matching_engine replay wal/commands.wal wal/events.wal
```

Dump stored execution events:

```bash
./build/debug/matching_engine dump-events wal/events.wal
```

Dump public book and trade tape from Execution Event WAL:

```bash
./build/debug/matching_engine dump-book wal/events.wal
./build/debug/matching_engine dump-trades wal/events.wal
```

Scenario format:

```text
NEW seq instrument client order side price_ticks quantity_lots GTC
CANCEL seq instrument client order
REPLACE seq instrument client old_order new_order side price_ticks quantity_lots GTC
```

Run the manual load benchmark after building the `load_pipeline_benchmark`
target:

```bash
./build/load_pipeline_benchmark 100000 build/load_benchmark 0
```

The third argument is `commit_every`. Supported values are `1`, `16`, `64`,
`256`, `1024`, and `0`; `0` means one commit at the end of each WAL write phase.
The benchmark is intentionally outside `ctest`.

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

Current prototype commit semantics:

```text
WalSegmentWriter::commit() -> std::ofstream::flush()
```

This is useful for separating append and flush costs in the prototype benchmark,
but it is not a durable storage guarantee. Durable `fsync` / `fdatasync`
semantics and the recovery contract for Event WAL batch commit remain future
work.

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
- OrderBook matching behavior for passive orders, aggressive fills, duplicate rejection, cancel behavior, and replace behavior.
- Instrument pipeline behavior through Command WAL, `InstrumentEngine`, Execution Event WAL, and typed event readback.
- Replay validation of regenerated events against stored event streams.
- Market data projection from Execution Event WAL into public book/trade state.
- CLI/demo runner smoke checked through scenario files.

Manual benchmarks are not part of `ctest`. See `benchmarks/README.md` and
`benchmarks/results/README.md` for the current load-pipeline measurements.

## Scope Guard

Out of scope for the first prototype:

- FIX/session management/authentication.
- Market data protocol.
- Database persistence.
- Risk engine and portfolio accounting.
- Accounts, balances, futures, margin, and liquidation.
- Self-trade prevention.
- Clustering, failover, consensus, and replicated durability.
- Advanced order types.
- Batch matching and Event WAL batch commit.

First meaningful prototype target:

- one instrument;
- limit buy/sell orders;
- cancel orders;
- replace orders with a new order id;
- price/time priority;
- command WAL;
- execution event WAL;
- deterministic replay test;
- demo runner from scenario file to Replay OK;
- market data projection from event WAL;
- raw and typed WAL adapters;
- simple file segment writer/reader.
