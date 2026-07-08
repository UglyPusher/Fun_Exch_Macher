# Benchmarks

Benchmarks are manual measurement tools, not unit tests.

They are intentionally not registered in `ctest` because their result depends on
CPU, filesystem, build type, and background load.

## `load_pipeline_benchmark`

Measures how many normalized commands can pass through the current prototype
pipeline and where the time is spent:

```text
generated OrderCommandRecordV1
    -> Command WAL append
    -> Command WAL read
    -> InstrumentEngine / OrderBook
    -> Event WAL append
```

The generated workload uses pairs:

```text
passive sell order
aggressive buy order crossing that sell
```

This keeps the book small while still exercising validation, matching,
TradeExecuted emission, WAL write, WAL read, and event WAL write.

Run after building:

```sh
./build/load_pipeline_benchmark 100000 build/load_benchmark 0
```

Arguments:

```text
1. command count, optional, default 100000
2. output directory for benchmark WAL files, optional, default benchmark_wal
3. legacy commit_every value, optional, default 0
```

WAL-v0 commits each successful append durably:

```text
append -> write -> flush -> fsync -> committed visibility
```

The third argument is retained so older benchmark commands still parse, but it
does not select a durability policy in WAL-v0.

Read the result as a local estimate, not a product guarantee. Use the same build
type and disk location when comparing changes.

The benchmark prints separate tables for counters, timings, and derived ratios.
Every row owns its own counters; for example, `read_match_event_pipeline`
reports its own commands read, events written, Event WAL commits, and Event WAL
bytes instead of reusing values from the matcher-only or event-WAL-only phases.

Counter columns:

```text
phase
command_count
event_count
record_count
commit_count
byte_count
```

Measured phases:

```text
generate_commands
command_wal_write
command_wal_read_only
matcher_only_without_event_wal
event_wal_append
read_match_event_pipeline
```

Use `matcher_only_without_event_wal` to estimate matcher cost. Use
`read_match_event_pipeline` to estimate the current end-to-end read/match/event
write path. Compare `event_wal_append` and `read_match_event_pipeline` at
different `commit_every` values to see how much commit frequency affects the
manual benchmark on the current machine.
