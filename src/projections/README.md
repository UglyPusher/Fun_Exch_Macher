# Projections

`src/projections/` owns downstream read models built from execution events.

The current implemented projection is `MarketDataProjection`, which reduces an
ordered `ExecutionEventRecordV1` stream into public book levels and public
trades.

## Owns

- Projected active order state.
- Aggregated bid and ask book view.
- Public trade tape.
- Event sequence continuity checks for the projection stream.

## Does Not Own

- Command validation.
- Order matching.
- Replay comparison.
- WAL reading.
- Command WAL interpretation.

Projection tests should use explicit execution events or a separate integration
test. A projection unit test must not call `InstrumentEngine` just to create its
input.
