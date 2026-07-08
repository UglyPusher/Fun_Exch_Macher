# Matching Engine — Current State Documentation

Date: 2026-06-19
Baseline archive: `matching_engine(1).zip`
Current code checkpoint: after commit `01cce98 Simplify order book matching flow`.
Status checked: CMake configure/build OK, `ctest` OK, 11/11 tests passed.

---

## 1. System contour

Current project is a small single-instrument matching-engine prototype with persistent command/event WAL files and replay verification.

The implemented runtime path is:

```text
scenario text file
    -> ScenarioLoader
    -> vector<OrderCommandRecordV1>
    -> command WAL writer
    -> InstrumentEngine
    -> OrderBook
    -> vector<ExecutionEventRecordV1>
    -> event WAL writer
    -> replay check
```

The architectural target visible in the code is:

```text
normalized order command stream
    -> per-instrument InstrumentEngine
    -> deterministic OrderBook reducer
    -> execution event stream
    -> replay / projections / dumps
```

The code currently implements one `InstrumentEngine` with one internal `OrderBook`. There is no external networking, no real ingress normalizer, no multi-instrument router, no storage engine beyond segment WAL files.

---

## 2. Top-level tree

```text
.
├── CMakeLists.txt
├── benchmarks/
│   ├── README.md
│   └── load_pipeline_benchmark.cpp
├── docs/
│   ├── CURRENT_STATE_DOCUMENTATION.md
│   ├── architecture.md
│   ├── matching_rules.md
│   ├── replay.md
│   ├── wal.md
│   └── architecture/
│       ├── current_architecture.md
│       └── expected_architecture.md
├── examples/
│   ├── simple_session.txt
│   ├── cancel_replace_session.txt
│   └── replay_mismatch_session.txt
├── src/
│   ├── README.md
│   ├── app/
│   ├── core/
│   ├── domain/
│   ├── projections/
│   └── wal/
├── tests/
│   ├── order_book_tests.cpp
│   ├── matching_tests.cpp
│   ├── replay_tests.cpp
│   ├── instrument_engine_wal_pipeline_tests.cpp
│   ├── market_data_projection_tests.cpp
│   └── wal/
└── wal/
    ├── demo_commands.wal
    ├── demo_events.wal
    ├── simple_commands.wal
    └── simple_events.wal
```

Each meaningful `src/` directory has a local `README.md` that states ownership,
forbidden responsibilities, and where the active code lives. Reserved scaffold
directories such as `src/matcher/` and `src/order_book/` are documented as
inactive to avoid accidental duplicate implementations.

---

## 3. Build targets and link dependencies

Defined by root `CMakeLists.txt` and `src/wal/CMakeLists.txt`.

```text
matching_engine_domain
    owns: record type enum implementation
    exposes: src/domain/include

matching_engine_wal
    owns: raw WAL segment reader/writer/scanner/checksum/header/result code
    exposes: src/wal/include

matching_engine_core
    owns: InstrumentEngine, OrderBook, ReplayRunner
    depends on: matching_engine_domain, matching_engine_wal

matching_engine_projections
    owns: MarketDataProjection
    depends on: matching_engine_domain

matching_engine_app
    owns: CLI, scenario loading, event dumping, WAL adapters
    depends on: matching_engine_core, matching_engine_projections
```

Current dependency graph:

```text
                 ┌──────────────────────────┐
                 │ matching_engine_app      │
                 └────────────┬─────────────┘
                              │
                 ┌────────────┴─────────────┐
                 │                          │
                 ▼                          ▼
       ┌──────────────────────┐   ┌──────────────────────────┐
       │ matching_engine_core │   │ matching_engine_projections│
       └──────────┬───────────┘   └────────────┬─────────────┘
                  │                            │
          ┌───────┴────────┐                   │
          ▼                ▼                   ▼
┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐
│ matching_engine_ │ │ matching_engine_ │ │ matching_engine_ │
│ domain           │ │ wal              │ │ domain           │
└──────────────────┘ └──────────────────┘ └──────────────────┘
```

`market_data_projection_tests` is now a clean projection test target. It links only `matching_engine_projections` and uses explicit `ExecutionEventRecordV1` test events instead of calling matcher or WAL code.

---

## 4. Module ownership

### 4.1 `src/domain`

Purpose: stable DTOs and enum contracts shared between WAL payloads, core, replay, app, and projections.

Files:

```text
src/domain/include/domain/order_command_record.hpp
src/domain/include/domain/execution_event_record.hpp
src/domain/include/domain/matching_types.hpp
src/domain/include/domain/record_types.hpp
src/domain/src/record_types.cpp
```

Main types:

```text
OrderCommandRecordV1
ExecutionEventRecordV1
CommandType
Side
TimeInForce
ExecutionEventType
RejectionReason
RecordType
```

Current design:

- Command/event records are trivially copyable DTOs.
- WAL typed reader/writer writes them by raw byte copy.
- Enum values are defined as scoped enums in `domain::matching_types.hpp`.
- DTO fields still store wire values as `uint16_t`, not enum fields.

Domain dependency rule:

```text
domain must not depend on core, wal, projections, or app
```

Current state: rule is respected.

---

### 4.2 `src/wal`

Purpose: append/read/scan one WAL segment file with record headers, segment headers, checksums, alignment, and typed wrappers.

Files:

```text
src/wal/include/wal/raw_wal_reader.hpp
src/wal/include/wal/raw_wal_writer.hpp
src/wal/include/wal/typed_wal_reader.hpp
src/wal/include/wal/typed_wal_writer.hpp
src/wal/include/wal/wal.hpp
src/wal/include/wal/wal_alignment.hpp
src/wal/include/wal/wal_checksum.hpp
src/wal/include/wal/wal_error.hpp
src/wal/include/wal/wal_file.hpp
src/wal/include/wal/wal_position.hpp
src/wal/include/wal/wal_record_header.hpp
src/wal/include/wal/wal_record_view.hpp
src/wal/include/wal/wal_result.hpp
src/wal/include/wal/wal_segment_header.hpp
src/wal/include/wal/wal_segment_reader.hpp
src/wal/include/wal/wal_segment_scanner.hpp
src/wal/include/wal/wal_segment_writer.hpp
src/wal/include/wal/wal_types.hpp
```

Main types:

```text
Wal
WalCursor
WalRecord
TypedWalWriter<TRecord, TRecordType>
TypedWalReader<TRecord, TRecordType>
WalSegmentWriter
WalSegmentReader
WalSegmentScanner
WalRecordHeader
WalSegmentHeader
WalPosition
WalAppendResult / WalBatchAppendResult / WalReadResult / WalBatchReadResult
WalError
```

Current WAL model:

```text
WalSegmentHeader
    -> WalRecordHeader + payload + padding
    -> WalRecordHeader + payload + padding
    -> ...
```

Responsibilities:

- `Wal` is the public durable committed-message facade.
- `WalSegmentWriter` is internal segment append/reopen logic.
- `WalSegmentReader` sequentially reads and validates records.
- `WalSegmentScanner` scans an existing segment and finds the last valid position/offset.
- `TypedWalReader/Writer` are thin adapters over `Wal` for trivially-copyable records.

Current dependency rule:

```text
wal must not depend on domain, core, projections, or app
```

Current state: rule is respected.

Important limitation:

- This is a local single-segment WAL implementation.
- There is no segment rotation.
- There is no replicated/quorum writer.
- There is no async batching interface above `commit()`.
- There is no semantic stream registry beyond numeric `StreamId` passed by the caller.

---

### 4.3 `src/core`

Purpose: deterministic matching and replay verification.

Files:

```text
src/core/include/core/instrument_engine.hpp
src/core/include/core/order_book.hpp
src/core/include/core/replay.hpp
src/core/include/core/matching_types.hpp
src/core/include/core/dummy_instrument_engine.hpp
src/core/src/instrument_engine.cpp
src/core/src/order_book.cpp
src/core/src/replay.cpp
src/core/src/dummy_instrument_engine.cpp
```

#### `InstrumentEngine`

Role:

```text
normalized OrderCommandRecordV1
    -> command-type dispatch
    -> OrderBook method
    -> vector<ExecutionEventRecordV1>
```

Owns:

- one `OrderBook` instance.

Does not own:

- WAL I/O;
- scenario parsing;
- projection;
- external services;
- multi-instrument routing.

Current dispatch state:

- Dispatch is explicit. Unknown command types are decoded as empty and routed to `OrderBook::reject_unsupported_command()`, not through the new-order path.

#### `OrderBook`

Role:

```text
OrderCommandRecordV1
    -> validate
    -> mutate in-memory book
    -> emit ExecutionEventRecordV1 records
```

Owns:

- price-time priority queues;
- active order index;
- event sequence allocation;
- trade id allocation;
- single-instrument binding/checking;
- matching rule implementation.

Internal state:

```text
instrument_id_: optional<uint32_t>
next_event_sequence_: uint64_t
next_trade_id_: uint64_t
bids_: map<price, deque<RestingOrder>, greater>
asks_: map<price, deque<RestingOrder>>
active_orders_: unordered_map<order_id, RestingOrder>
```

Public API:

```text
apply_new_order(command) -> vector<ExecutionEventRecordV1>
apply_cancel_order(command) -> vector<ExecutionEventRecordV1>
apply_replace_order(command) -> vector<ExecutionEventRecordV1>
reject_unsupported_command(command) -> vector<ExecutionEventRecordV1>
has_order(order_id)
best_bid_price()
best_ask_price()
remaining_quantity(order_id)
active_order_count()
validate_invariants()
snapshot()
```

Current matching semantics visible from code:

- only `GTC` exists;
- `NewOrder` validates type/side/price/quantity/duplicate id/instrument/time-in-force;
- `CancelOrder` cancels existing active order;
- `ReplaceOrder` is modeled as cancel old order + submit replacement order;
- trade event uses incoming order as `order_id` and resting order as `contra_order_id`;
- order accepted/rejected/rested/filled/cancelled events are emitted by `OrderBook`.
- raw enum fields are decoded into `std::optional<CommandType>`, `std::optional<Side>`, and `std::optional<TimeInForce>` before domain use.

Current readability state:

- `order_book.cpp` is still the largest source file: 634 lines.
- `order_book.hpp` is smaller and more navigable after cleanup: 129 lines.
- The artificial "calculate trade plan, then apply trade plan" layer has been removed.
- Matching now applies resting-order mutation and emits `TradeExecuted` in the same local flow.
- Dead/transitional helpers from the previous refactor were removed.
- Buy/sell matching still has two explicit loops. This is intentional for readability; only shared cancellation mechanics were factored out.

#### `ReplayRunner`

Role:

```text
command WAL reader + stored event WAL reader
    -> replay commands through InstrumentEngine
    -> compare generated events with stored events
    -> report first divergence
```

Owns:

- deterministic replay loop;
- command sequence gap detection;
- stored/generated event comparison;
- invariant validation after each command;
- failure diagnostics with before/after book snapshots.

Interfaces:

```text
CommandLogReader
EventLogReader
EventComparator
ReplayRunner
ReplayResult / ReplayFailure
```

Current dependency note:

- Replay interfaces are abstract and do not require WAL directly.
- `matching_engine_core` nevertheless links `matching_engine_wal` at target level, although core headers do not need to include WAL headers. This is probably because app/tests use core together with WAL, not because core itself currently uses WAL internally. This should be reviewed later.

---

### 4.4 `src/projections`

Purpose: derive public market data state from execution events.

Files:

```text
src/projections/include/projections/market_data_projection.hpp
src/projections/src/market_data_projection.cpp
```

Main types:

```text
MarketDataProjection
PublicBookView
PublicPriceLevel
PublicTrade
ProjectionApplyResult
```

Role:

```text
ExecutionEventRecordV1 stream
    -> MarketDataProjection.apply(event)
    -> PublicBookView + trade tape
```

Owns:

- projected active orders;
- aggregated bid/ask levels;
- public trades;
- last applied event sequence.

Does not own:

- matching;
- replay comparison;
- WAL reading;
- command validation.

Current semantics:

- `OrderAccepted` and `OrderRejected` are ignored by projection.
- `OrderRested` and `OrderPartiallyFilled` upsert visible order state.
- `TradeExecuted` appends a public trade and reduces projected resting order quantity.
- `OrderFullyFilled` removes if present, otherwise ignores.
- `OrderCancelled` removes existing projected order.
- Projection enforces contiguous event sequence.

Current dependency rule:

```text
projections may depend on domain
projections must not depend on core or wal
```

Source target state: rule is respected.
Test target state: rule is respected. `market_data_projection_tests` links only `matching_engine_projections`.

---

### 4.5 `src/app`

Purpose: demo/CLI adapter layer.

Files:

```text
src/app/main.cpp
src/app/scenario_loader.hpp
src/app/scenario_loader.cpp
src/app/event_dump.hpp
src/app/event_dump.cpp
```

Commands:

```text
matching_engine run <scenario> [command.wal] [event.wal]
matching_engine replay <command.wal> <event.wal>
matching_engine dump-events <event.wal>
matching_engine dump-book <event.wal>
matching_engine dump-trades <event.wal>
```

Responsibilities:

- parse simple scenario text files into `OrderCommandRecordV1` records;
- write command WAL;
- run `InstrumentEngine` over loaded commands;
- write execution event WAL;
- replay written WALs for verification;
- dump events;
- build book/trade views from event WAL through `MarketDataProjection`.

Current state:

- `main.cpp` is large: 389 lines.
- It owns CLI routing, WAL setup constants, adapter classes, run/replay/dump implementation, and output formatting glue.
- This is acceptable for a prototype, but it is already too much for a stable CLI module.

---

## 5. Data contracts

### 5.1 `OrderCommandRecordV1`

```text
command_sequence
source_ingress_epoch
source_ingress_sequence
order_id
replacement_order_id
client_id
price_ticks
quantity_lots
instrument_id
command_type
side
time_in_force
reserved
```

Meaning:

- normalized command DTO;
- binary WAL payload;
- sequence is expected to be contiguous during replay;
- `replacement_order_id` is used by replace flow.

Current caveat:

- `command_type`, `side`, and `time_in_force` are raw integer fields. Core must decode them before domain use.

### 5.2 `ExecutionEventRecordV1`

```text
event_sequence
command_sequence
source_ingress_epoch
source_ingress_sequence
order_id
contra_order_id
trade_id
price_ticks
quantity_lots
remaining_quantity_lots
instrument_id
event_type
side
rejection_reason
reserved
```

Meaning:

- durable facts emitted by matching;
- replay compares stored facts against regenerated facts;
- projection consumes public-relevant event types.

Current caveat:

- `event_type`, `side`, and `rejection_reason` are raw integer fields. Consumers cast to enum values.

---

## 6. Runtime flows

### 6.1 Run flow

```text
app::load_scenario(file)
    -> vector<OrderCommandRecordV1>

write_commands(command_wal, commands)
    -> WalSegmentWriter
    -> TypedWalWriter<OrderCommandRecordV1, OrderCommand>

run_engine(commands)
    -> InstrumentEngine.apply(command)
    -> OrderBook.apply_*()
    -> vector<ExecutionEventRecordV1>

write_events(event_wal, events)
    -> WalSegmentWriter
    -> TypedWalWriter<ExecutionEventRecordV1, ExecutionEvent>

replay_wals(command_wal, event_wal)
    -> ReplayRunner
```

### 6.2 Replay flow

```text
WalSegmentReader(command.wal)
    -> TypedWalReader<OrderCommandRecordV1, OrderCommand>
    -> WalCommandReplayReader

WalSegmentReader(event.wal)
    -> TypedWalReader<ExecutionEventRecordV1, ExecutionEvent>
    -> WalEventReplayReader

ReplayRunner.replay(...)
    loop:
        read command
        validate command_sequence continuity
        snapshot before
        engine.apply(command)
        snapshot after
        read same number of stored events
        compare each generated/stored event
        validate OrderBook invariants
    after command EOF:
        reject extra stored event
```

### 6.3 Projection flow

```text
WalSegmentReader(event.wal)
    -> TypedWalReader<ExecutionEventRecordV1, ExecutionEvent>
    -> MarketDataProjection.apply(event)
    -> PublicBookView / trades
```

---

## 7. Tests

Current test targets:

```text
order_book_tests
matching_tests
replay_tests
instrument_engine_wal_pipeline_tests
market_data_projection_tests
wal_record_header_tests
wal_segment_writer_tests
wal_segment_reader_tests
wal_recovery_tests
typed_wal_tests
wal_checksum_tests
```

Observed result:

```text
11/11 passed
```

Coverage direction:

- `OrderBook`: unit-level matching behavior and invariants.
- `InstrumentEngine + WAL`: pipeline behavior.
- `ReplayRunner`: stored/generated event comparison.
- `MarketDataProjection`: public book/trade derivation.
- `WAL`: header, writer, reader, recovery/scanner, typed reader/writer, checksum.

---

## 8. Current dependency boundaries

### Clean boundaries

```text
domain -> no project dependencies
wal    -> no project dependencies
projection source -> domain only
OrderBook -> domain/core types only, no I/O
ReplayRunner -> abstract readers, no direct WAL dependency in source
```

### Blurred boundaries

```text
matching_engine_core target links matching_engine_wal
app/main.cpp owns too much orchestration
core/matching_types.hpp aliases domain enums, preserving old include path
```

Notes:

- `matching_engine_core` still links `matching_engine_wal` at target level even though core source files do not directly need WAL APIs for `OrderBook` or `InstrumentEngine`.
- `OrderBook` is cleaner after the latest refactor, but it remains the largest core module and should stay under review.

### Boundary to protect

The most important design boundary remains:

```text
Command WAL / normalized command DTO
    -> matcher core
    -> Execution Event WAL / event DTO
```

`OrderBook` must not learn about WAL, files, CLI, scenario text format, projections, or networking.

---

## 9. Current line-count hotspots

Measured from current working tree:

```text
src/core/src/order_book.cpp                         634 lines
src/app/main.cpp                                    389 lines
src/wal/src/wal_segment_writer.cpp                 233 lines
src/projections/src/market_data_projection.cpp     223 lines
src/app/scenario_loader.cpp                        186 lines
src/core/src/replay.cpp                            174 lines
src/core/include/core/order_book.hpp               129 lines
```

Interpretation:

- `OrderBook` is still the main structural hotspot, but the previous over-split helper layer was partially unwound.
- `app/main.cpp` is the second hotspot, but acceptable for demo CLI for now.
- WAL files are not currently the primary readability problem.
- Projection is medium-sized and still reasonably bounded.

---

## 10. Current structural risks

### 10.1 `OrderBook` remains the main core hotspot

It owns legitimate domain state and the latest cleanup removed the worst artificial helper layer. It is still the largest source file and should be changed cautiously.

Current shape:

```text
apply_new_order:
    validate
    accept
    match against opposite book
    rest or fully-fill

match_buy_order_against_asks / match_sell_order_against_bids:
    walk best opposite level
    mutate resting order
    emit TradeExecuted
    remove empty resting order/level
```

Remaining concern:

```text
The buy and sell matching loops are intentionally explicit and similar.
Do not replace them with a generic abstraction unless it makes price-time priority easier to read.
```

### 10.2 Dispatch must remain explicit

`InstrumentEngine::apply()` now presents a clean command dispatch map. Keep it that way.

Current shape:

```text
decode command type
if Unknown      -> reject_unsupported_command
if NewOrder     -> apply_new_order
if CancelOrder  -> apply_cancel_order
if ReplaceOrder -> apply_replace_order
```

### 10.3 Enum decoding is explicit in core

Current DTOs correctly store raw wire values. Core decode helpers return optionals and do not manufacture fake enum values for invalid input.

Current shape:

```text
optional<CommandType>
optional<Side>
optional<TimeInForce>
```

### 10.4 Projection dependency is clean in source and tests

`matching_engine_projections` depends only on domain. `market_data_projection_tests` now also depends only on the projection target. Integration between matcher, WAL, and projection should stay in separate integration tests.

### 10.5 `app/main.cpp` is a demo orchestrator, not a stable application layer

Eventually split into:

```text
cli_parser
wal_adapters
run_command
replay_command
dump_commands
```

Do not do this as a cosmetic split. Extract only named boundaries that reduce orchestration load and keep tests meaningful.

---

## 11. What exists vs what does not exist yet

### Exists

```text
single-instrument deterministic OrderBook
new/cancel/replace order handling
execution event generation
command/event DTOs
single-segment WAL writer/reader/scanner
binary typed WAL wrappers
replay verification
market-data projection from events
CLI demo and dump commands
unit/integration tests
```

### Does not exist yet

```text
real external ingress
normalizer/balancer
multi-instrument router
per-instrument command WAL registry
replicated/quorum WAL writer
segment rotation
snapshotting/checkpointing
client/public query API
risk/account/funds checks
auth/session stream
data-query stream
backpressure/degrade mode
network protocol
persistent DB mirror
```

---

## 12. Recommended next Codex task

Use this as the next precise instruction, not a vague “improve readability” command:

```text
Align target dependencies and app orchestration boundaries without changing matching behavior.

Hard constraints:
1. All existing tests must pass.
2. Do not change OrderBook behavior.
3. Review why matching_engine_core links matching_engine_wal at target level.
4. If the link is unnecessary, remove it and move WAL coupling to app/tests.
5. Keep MarketDataProjection independent from core and wal.
6. Do not split app/main.cpp unless the extracted boundary has a clear name and tests still cover run/replay/dump behavior.
7. Update current architecture documentation after any dependency change.
```

---

## 13. Current state verdict

The architecture is still recognizable and not broken:

```text
domain DTOs
+ WAL infrastructure
+ deterministic core reducer
+ event-sourced replay
+ downstream projection
```

The implementation risk is still highest around `OrderBook`, but the latest cleanup restored a more direct matching flow and removed the worst over-split helper layer. The next risk is dependency drift: core target linkage, app orchestration, and tests should keep proving that matcher, WAL, replay, and projection remain separate stages.
