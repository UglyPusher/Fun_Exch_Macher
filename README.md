# Matching Engine

Skeleton for a C++ matching engine project.

## Layout

- `docs/` - architecture notes and behavior documentation.
- `src/domain/` - core domain types such as orders, trades, and identifiers.
- `src/wal/` - write-ahead log storage and recovery logic.
- `src/matcher/` - matching algorithms and trade generation.
- `src/order_book/` - order book data structures.
- `src/app/` - application entry points and session wiring.
- `tests/` - unit and replay tests.
- `examples/` - sample input sessions.

## Build

```bash
cmake --workflow --preset build
cmake --workflow --preset test
cmake --workflow --preset run
```

## Debug

```bash
cmake --workflow --preset debug
```

## Lower-level CMake Commands

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cmake --build --preset run
cmake --build --preset debug-run
```
