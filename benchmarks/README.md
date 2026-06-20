# Benchmarks

Benchmarks are manual measurement tools, not unit tests.

They are intentionally not registered in `ctest` because their result depends on
CPU, filesystem, build type, and background load.

## `load_pipeline_benchmark`

Measures how many normalized commands can pass through the current prototype
pipeline and where the time is spent:

```text
generated OrderCommandRecordV1
    -> Command WAL append + commit
    -> Command WAL read
    -> InstrumentEngine / OrderBook
    -> Event WAL append + commit
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
./build/load_pipeline_benchmark 100000 build/load_benchmark
```

Arguments:

```text
1. command count, optional, default 100000
2. output directory for benchmark WAL files, optional, default benchmark_wal
```

Read the result as a local estimate, not a product guarantee. Use the same build
type and disk location when comparing changes.

Reported counters:

```text
generated_commands
command_wal_records_written
command_wal_commits
command_wal_records_read
commands_matched
execution_events_emitted
event_wal_records_written
event_wal_commits
events_per_command
event_commits_per_command
events_per_event_commit
```

Reported timing sections:

```text
command_wal_write_and_commit
command_wal_read_only
matcher_only_without_event_wal
event_wal_append_and_commit
read_match_event_pipeline
```

Use `matcher_only_without_event_wal` to estimate matcher cost. Use
`read_match_event_pipeline` to estimate the current end-to-end read/match/event
write path.
