# Core Matching Layer

`src/core/` owns deterministic matching and replay verification.

The core layer consumes normalized domain records and emits domain execution
events. It is not a service layer and must stay independent from CLI parsing,
network protocols, market-data projection, and direct file orchestration inside
matching logic.

## Main Types

```text
InstrumentEngine     Routes one command to one OrderBook.
OrderBook            Deterministic in-memory order book for one instrument.
ReplayRunner         Replays command streams and compares stored events.
EventComparator      Compares replay-generated and stored event records.
```

## OrderBook Boundary

`OrderBook` owns:

- price-time priority;
- active order indexing;
- event sequence allocation;
- trade id allocation;
- single-instrument consistency.

`OrderBook` must not read WAL files, write WAL files, parse scenarios, call
projections, or depend on wall-clock time.

## Replay Boundary

Replay uses abstract command/event readers. App or tests may adapt WAL readers
into those interfaces, but replay logic itself should stay about deterministic
comparison and failure diagnostics.

See `../../docs/matching_rules.md` and `../../docs/replay.md`.
