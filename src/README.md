# Source Layout

`src/` is split by system responsibility, not by C++ convenience.

The implemented runtime path is:

```text
app scenario / CLI
    -> domain OrderCommandRecordV1
    -> core InstrumentEngine / OrderBook
    -> domain ExecutionEventRecordV1
    -> wal storage / replay / projections
```

## Directories

```text
app/            CLI adapter, scenario loading, event dumping, WAL wiring.
core/           Deterministic matching and replay verification.
domain/         Stable wire DTOs and durable enum contracts.
projections/    Downstream read models built from execution events.
wal/            Payload-agnostic binary write-ahead log implementation.
matcher/        Reserved scaffold; no active code lives here yet.
order_book/     Reserved scaffold; the active order book is in core/.
```

## Boundary Rules

- `domain` must not depend on any other project module.
- `wal` must not depend on trading semantics.
- `core` must not do file, CLI, network, or projection work inside matching logic.
- `projections` consume execution events only; they must not call the matcher.
- `app` is allowed to wire modules together, but it should not own domain rules.

See `../docs/CURRENT_STATE_DOCUMENTATION.md` for the current stage map.
Manual throughput tools live in `../benchmarks/`.
