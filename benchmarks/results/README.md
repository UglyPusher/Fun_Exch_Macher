# Load Pipeline Benchmark Results

These results were collected from the manual `load_pipeline_benchmark`.
They are local measurements, not product guarantees.

The benchmark was run on the same working tree, build directory, filesystem
path, and command count for every completed row.

```text
date: 2026-06-21
git revision: d1bb71f Add load pipeline benchmark analytics
build type: Debug
generator: Unix Makefiles
command count: 100000
workload: passive sell order, then aggressive buy order crossing that sell
output paths: build/load_benchmark_ce*
```

Important WAL note:

```text
Current WAL-v0 append commits with write, flush, fsync, and committed
visibility. Historical rows that vary `commit_every` should be read as
pre-refactor measurements, not current durability-policy choices.
```

## Comparison

| commit_every | total commands | total events | events/command | command WAL write commands/sec | event WAL append events/sec | read/match/event pipeline commands/sec | event WAL commits | pipeline event WAL commits | command WAL bytes | event WAL bytes | pipeline event WAL bytes |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 100000 | 250000 | 2.5 | 16993 | 17163 | 4991 | 1 | 1 | 13600040 | 38000040 | 38000040 |
| 1 | not completed | not completed | not completed | not completed | not completed | not completed | not completed | not completed | not completed | not completed | not completed |
| 16 | 100000 | 250000 | 2.5 | 6486 | 6333 | 1851 | 15625 | 15625 | 13600040 | 38000040 | 38000040 |
| 64 | 100000 | 250000 | 2.5 | 12039 | 10708 | 3320 | 3907 | 3907 | 13600040 | 38000040 | 38000040 |
| 256 | 100000 | 250000 | 2.5 | 14080 | 11602 | 4085 | 977 | 977 | 13600040 | 38000040 | 38000040 |
| 1024 | 100000 | 250000 | 2.5 | 18516 | 16314 | 4590 | 245 | 245 | 13600040 | 38000040 | 38000040 |

`commit_every=1` was started with the same command count and output directory
pattern, but it was interrupted after more than 10 minutes while blocked in disk
I/O. Treat it as a pathological diagnostic case for this machine and filesystem
path, not as a completed throughput measurement.

## Detailed Completed Rows

### commit_every=0

| phase | commands | events | records | commits | bytes | seconds | commands/sec | events/sec | records/sec |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| generate_commands | 100000 | 0 | 0 | 0 | 0 | 0.016 | 6227786 | 0 | 0 |
| command_wal_write | 100000 | 0 | 100000 | 1 | 13600040 | 5.885 | 16993 | 0 | 16993 |
| command_wal_read_only | 100000 | 0 | 100000 | 0 | 13600040 | 3.166 | 31589 | 0 | 31589 |
| matcher_only_without_event_wal | 100000 | 250000 | 0 | 0 | 0 | 0.284 | 351583 | 878958 | 0 |
| event_wal_append | 0 | 250000 | 250000 | 1 | 38000040 | 14.567 | 0 | 17163 | 17163 |
| read_match_event_pipeline | 100000 | 250000 | 250000 | 1 | 38000040 | 20.038 | 4991 | 12476 | 12476 |

### commit_every=16

| phase | commands | events | records | commits | bytes | seconds | commands/sec | events/sec | records/sec |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| generate_commands | 100000 | 0 | 0 | 0 | 0 | 0.024 | 4146669 | 0 | 0 |
| command_wal_write | 100000 | 0 | 100000 | 6250 | 13600040 | 15.418 | 6486 | 0 | 6486 |
| command_wal_read_only | 100000 | 0 | 100000 | 0 | 13600040 | 3.226 | 30995 | 0 | 30995 |
| matcher_only_without_event_wal | 100000 | 250000 | 0 | 0 | 0 | 0.248 | 403102 | 1007755 | 0 |
| event_wal_append | 0 | 250000 | 250000 | 15625 | 38000040 | 39.474 | 0 | 6333 | 6333 |
| read_match_event_pipeline | 100000 | 250000 | 250000 | 15625 | 38000040 | 54.030 | 1851 | 4627 | 4627 |

### commit_every=64

| phase | commands | events | records | commits | bytes | seconds | commands/sec | events/sec | records/sec |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| generate_commands | 100000 | 0 | 0 | 0 | 0 | 0.021 | 4820320 | 0 | 0 |
| command_wal_write | 100000 | 0 | 100000 | 1563 | 13600040 | 8.307 | 12039 | 0 | 12039 |
| command_wal_read_only | 100000 | 0 | 100000 | 0 | 13600040 | 3.007 | 33252 | 0 | 33252 |
| matcher_only_without_event_wal | 100000 | 250000 | 0 | 0 | 0 | 0.326 | 306548 | 766371 | 0 |
| event_wal_append | 0 | 250000 | 250000 | 3907 | 38000040 | 23.347 | 0 | 10708 | 10708 |
| read_match_event_pipeline | 100000 | 250000 | 250000 | 3907 | 38000040 | 30.121 | 3320 | 8300 | 8300 |

### commit_every=256

| phase | commands | events | records | commits | bytes | seconds | commands/sec | events/sec | records/sec |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| generate_commands | 100000 | 0 | 0 | 0 | 0 | 0.017 | 6001668 | 0 | 0 |
| command_wal_write | 100000 | 0 | 100000 | 391 | 13600040 | 7.102 | 14080 | 0 | 14080 |
| command_wal_read_only | 100000 | 0 | 100000 | 0 | 13600040 | 3.378 | 29605 | 0 | 29605 |
| matcher_only_without_event_wal | 100000 | 250000 | 0 | 0 | 0 | 0.403 | 247874 | 619685 | 0 |
| event_wal_append | 0 | 250000 | 250000 | 977 | 38000040 | 21.548 | 0 | 11602 | 11602 |
| read_match_event_pipeline | 100000 | 250000 | 250000 | 977 | 38000040 | 24.482 | 4085 | 10212 | 10212 |

### commit_every=1024

| phase | commands | events | records | commits | bytes | seconds | commands/sec | events/sec | records/sec |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| generate_commands | 100000 | 0 | 0 | 0 | 0 | 0.017 | 5818817 | 0 | 0 |
| command_wal_write | 100000 | 0 | 100000 | 98 | 13600040 | 5.401 | 18516 | 0 | 18516 |
| command_wal_read_only | 100000 | 0 | 100000 | 0 | 13600040 | 3.930 | 25445 | 0 | 25445 |
| matcher_only_without_event_wal | 100000 | 250000 | 0 | 0 | 0 | 0.290 | 344515 | 861287 | 0 |
| event_wal_append | 0 | 250000 | 250000 | 245 | 38000040 | 15.324 | 0 | 16314 | 16314 |
| read_match_event_pipeline | 100000 | 250000 | 250000 | 245 | 38000040 | 21.786 | 4590 | 11475 | 11475 |

## Reading The Numbers

The workload produced a stable `2.5` execution events per command. That means a
pipeline rate of `4991 commands/sec` at `commit_every=0` is also about
`12476 event records/sec` in the full read/match/event path.

The matcher-only phase is not the current bottleneck in this Debug run:

```text
matcher_only_without_event_wal: 247874 to 403102 commands/sec
read_match_event_pipeline:       1851 to   4991 commands/sec
```

Commit frequency is visible even though the current prototype commit is only a
C++ stream flush:

```text
event_wal_append events/sec:
commit_every=16      6333
commit_every=64     10708
commit_every=256    11602
commit_every=1024   16314
commit_every=0      17163
```

`commit_every=1024` is close to `commit_every=0` for Event WAL append on this
machine. `commit_every=16` is still much slower. The result points first at
commit frequency and Event WAL volume, not at matching logic.

These numbers should be repeated in a Release build before making product-level
throughput claims.
