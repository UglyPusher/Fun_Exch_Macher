# TODO

This file tracks future work that is worth remembering but is not required for
the current prototype stage.

Keep entries short and actionable. Do not use this file as a scratchpad.

## Architecture

### Decide Whether `src/matcher/` Should Become an Active Layer

Current state:

- active matching code lives in `src/core/`;
- `InstrumentEngine` routes commands;
- `OrderBook` owns the deterministic matching FSM;
- `src/matcher/` is an empty reserved scaffold.

Question:

```text
Should matcher become a separate module, or should matching remain inside core?
```

Why this matters:

- a separate matcher layer may make responsibilities clearer later;
- moving code too early may create churn without improving behavior;
- replay event sequence and matching semantics must not change during any move.

Do this only as an explicit architecture refactor with tests proving unchanged
matching behavior.

### Batch Processing and Event WAL Batch Commit

Do not implement batching yet. First document and test the durability contract.

#### Batch Matching

Current prototype path:

```text
read one command
    -> apply matcher
    -> write emitted execution events
```

Target batch path:

```text
read committed command batch
    -> apply commands in strict command_sequence order
    -> collect execution events
    -> write execution events
```

Hard rules:

- Batch processing must not change matching semantics.
- Commands inside a batch are processed strictly in command sequence order.
- The matcher remains deterministic.
- A batch is an execution optimization, not a semantic unit of matching.
- No command may observe future commands from the same batch.
- Event order must be identical to single-command execution.

Expected benefits:

- lower reader overhead;
- lower function-call and dispatch overhead;
- better cache locality;
- natural integration with event WAL batch append;
- cleaner benchmark separation between matcher cost and WAL cost.

Out of scope for prototype-v0:

- parallel matching inside one instrument;
- command reordering;
- cross-instrument batching;
- speculative matching;
- client-visible batch semantics.

#### Event WAL Batch Commit

Batch commit is a durability-boundary change, not just an fsync optimization.

Current simple model:

```text
process command
    -> append event records
    -> commit Event WAL
```

Target model:

```text
process command batch
    -> append all emitted event records
    -> commit Event WAL once per batch or time slice
```

Supported future commit triggers:

- max event count, for example 256 / 1024 events;
- max command count, for example 256 / 1024 commands;
- max latency window, for example 1 ms;
- explicit flush on shutdown or test end.

Required committed-batch metadata:

- first covered command sequence;
- last covered command sequence;
- first event sequence;
- last event sequence;
- number of event records;
- batch checksum or final WAL block integrity status.

Recovery rule:

```text
last durable event batch covers command_sequence <= X
    -> restore state
    -> replay Command WAL from X + 1
    -> re-emit missing events deterministically
```

Hard rules:

- Command accepted into the system means Command WAL is durable.
- Execution result durable means Execution Event WAL is durable.
- Client-visible final execution response can be acknowledged only after the
  corresponding Execution Event WAL batch is durable.
- Reprocessing after crash must not duplicate committed execution events.
- The same Command WAL prefix must always produce the same Execution Event WAL prefix.
- Partial, torn, or uncommitted event batches are ignored during recovery.
- Missing events after the last durable batch are regenerated from Command WAL.
- Batch commit must be benchmarked separately from matcher speed.

Required benchmark modes before implementation:

```text
full_pipeline_commit_per_record
full_pipeline_commit_batch_16
full_pipeline_commit_batch_64
full_pipeline_commit_batch_256
full_pipeline_commit_batch_1024
matcher_only_batch_1
matcher_only_batch_64
matcher_only_batch_256
event_wal_append_only
event_wal_batch_commit_only
```

Required benchmark counters:

- commands processed;
- events emitted;
- event records per command;
- command WAL commits;
- event WAL commits;
- commands per event WAL commit;
- events per event WAL commit;
- bytes written;
- commands/sec;
- events/sec;
- commits/sec.

Implementation order:

1. Add benchmark support for batch sizes.
2. Add batch matcher loop without changing matching semantics.
3. Add Event WAL batch append API.
4. Document Event WAL batch commit recovery contract.
5. Add corrupted/uncommitted event batch recovery tests.
6. Only then enable batch commit in the full pipeline benchmark.
