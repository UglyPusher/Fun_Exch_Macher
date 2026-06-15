# Dual-lane integrity: reader/matcher corruption problem

## Status

This document describes an integrity problem and possible mitigation strategies for a stricter production-grade matching engine design.

This mechanism is **not included in the first prototype**.

The first prototype uses:

```text
InstrumentCommandWAL
    -> single WAL reader
    -> single matcher
    -> ExecutionEventWAL
```

The prototype relies on:

- WAL header/payload CRC validation;
- typed reader validation;
- order book invariants;
- deterministic replay;
- replay comparison between generated execution events and stored `ExecutionEventWAL`.

Dual-lane execution is a possible later extension.

---

# Problem statement

A WAL record may be correct on disk and may pass CRC validation in the WAL reader, but the command can still be corrupted after the reader has validated it and before the matcher applies it.

Example failure path:

```text
InstrumentCommandWAL record is valid
    ↓
Raw WAL reader validates header CRC and payload CRC
    ↓
Typed reader decodes payload into OrderCommandRecordV1
    ↓
command object / queue node / buffer is corrupted in memory
    ↓
matcher receives corrupted command
    ↓
order book state may be mutated incorrectly
```

This is not WAL storage corruption. It is **in-memory read/compute lane corruption**.

Possible causes:

```text
- RAM bit flip;
- CPU cache corruption;
- DMA or memory-controller error;
- data race;
- undefined behavior;
- use-after-free;
- buffer overwrite;
- incorrect object lifetime;
- compiler or optimization bug;
- broken hardware.
```

A checksum stored in the WAL record protects the bytes stored in the WAL. It does not automatically protect every later in-memory copy of the decoded command.

---

# Cannot be made impossible

This class of failure cannot be eliminated completely.

The system can only:

```text
1. reduce probability;
2. detect divergence;
3. fail-stop before committing false facts;
4. recover from a trusted durable source through replay.
```

Even a heavily protected system still has common-mode failure classes:

```text
- the same software bug in both execution paths;
- a semantically wrong but CRC-valid command already committed to CommandWAL;
- corrupted source-of-truth WAL without replica recovery;
- a comparator bug;
- hardware failure affecting all lanes;
- operator or deployment error.
```

---

# Minimal prototype policy

The first prototype does **not** attempt to solve in-memory command corruption with dual-lane runtime execution.

Prototype policy:

```text
- Raw WAL reader validates physical record integrity.
- Typed WAL reader validates record type and payload size.
- Matcher assumes valid in-process memory after successful reader validation.
- Matcher never applies a command if the reader reports a read failure.
- Replay is used to detect divergence from deterministic behavior.
```

This is acceptable for the prototype because the goal is to demonstrate:

```text
- deterministic reducer;
- explicit WAL boundaries;
- clean reader/matcher separation;
- replayability;
- order book invariants;
- append-only execution facts.
```

The prototype is not intended to provide production-grade protection against silent memory corruption.

---

# Solution option 1: command envelope checksum

A moderate single-lane mitigation is to pass commands as immutable validated envelopes.

Example:

```cpp
struct ValidatedOrderCommand
{
    wal::WalPosition position {};
    domain::OrderCommandRecordV1 record {};
    std::uint32_t checksum = 0;

    [[nodiscard]] bool has_valid_checksum() const noexcept;
};
```

The reader builds the envelope only after the WAL record has passed validation:

```text
Raw WAL reader validates record header and payload
    ↓
Typed reader validates record type and payload size
    ↓
DTO is copied into ValidatedOrderCommand
    ↓
checksum is calculated over DTO bytes
    ↓
immutable envelope is passed to matcher
```

The matcher checks the envelope before mutating the order book:

```text
if envelope checksum is valid:
    apply command
else:
    stop affected instrument stream
```

This protects against some corruption between typed decoding and matcher application.

Limitations:

```text
- does not protect against deterministic software bugs in matcher logic;
- does not protect against corruption inside order book state after command validation;
- does not prove which component is wrong;
- does not eliminate common-mode failures.
```

---

# Solution option 2: two matchers after one reader

Another option is to use one reader and two matcher instances:

```text
InstrumentCommandWAL
    ↓
single WAL reader
    ↓
validated command envelope
    ↓
fan-out
    ├── Matcher A -> CandidateEvents A
    └── Matcher B -> CandidateEvents B

CandidateEvents A/B
    ↓
Comparator
    ↓
ExecutionEventWAL append only if identical
```

This helps detect corruption or logic divergence in one matcher path.

It can detect:

```text
- bit flip in one order book instance;
- data race or memory overwrite affecting one matcher;
- corruption during one matcher computation;
- divergence between optimized matcher and reference matcher.
```

Important limitation:

```text
If both matchers receive the same already-corrupted command object, both may produce the same wrong result.
```

Therefore, this option does not fully protect the reader-to-matcher handoff boundary unless combined with command envelope validation.

---

# Solution option 3: dual reader + dual matcher lanes

A stronger design is to duplicate the whole read/compute lane.

```text
InstrumentCommandWAL
    ├── Reader A -> Typed decode A -> Matcher A -> CandidateEvents A
    └── Reader B -> Typed decode B -> Matcher B -> CandidateEvents B

CandidateEvents A
CandidateEvents B
    ↓
Comparator
    ↓
ExecutionEventWAL append only after agreement
```

This removes the single shared decoded-command object before the matcher.

Each lane independently reads the same WAL position, decodes the command, applies it to its own matcher state, and produces candidate execution events.

---

# Dual-lane synchronization rule

The two readers must be synchronized by `WalPosition`, not by timing.

Correct rule:

```text
Reader A reads command at {stream_id, epoch, sequence=N}
Reader B reads command at {stream_id, epoch, sequence=N}

Comparator verifies:
    position A == position B
    command hash A == command hash B
    command bytes A == command bytes B, if required
```

Incorrect rule:

```text
Reader A reads next command
Reader B reads next command
assume they are the same command
```

`stream_id + epoch + sequence` is the command identity.

---

# Dual-lane step pipeline

A strict dual-lane controller can process each command as follows.

## Stage 1: read agreement

```text
1. Reader A reads command N.
2. Reader B reads command N.
3. Compare command envelopes:
   - same WalPosition;
   - same record type;
   - same payload length;
   - same payload CRC or DTO checksum;
   - same DTO bytes if needed.
4. If mismatch: fail-stop before matcher.
```

## Stage 2: compute agreement

```text
1. Matcher A applies command N to book A.
2. Matcher B applies command N to book B.
3. Compare candidate execution events.
4. Optionally compare book state hash after command N.
5. If mismatch: fail-stop before ExecutionEventWAL.
```

## Stage 3: execution commit

```text
1. If read agreement and compute agreement both passed:
   append event batch to ExecutionEventWAL.
2. If EventWAL commit succeeds:
   execution result becomes accepted fact.
3. If EventWAL commit fails:
   stop or retry according to commit policy.
```

---

# Transaction acceptance semantics

The term “accepted transaction” must be used carefully.

A command can be accepted at the command-log level before its matching result is accepted.

```text
Command accepted:
    command record is committed in InstrumentCommandWAL
    or command can be deterministically rebuilt from TradingIngressWAL

Execution accepted:
    both read/compute lanes agree
    candidate execution events are identical
    event batch is committed to ExecutionEventWAL
```

In this design, the comparator gates **execution acceptance**, not initial external ingress acceptance.

---

# Comparator responsibility

The comparator must verify both input agreement and output agreement.

Input comparison:

```text
- WalPosition;
- record type;
- payload size;
- payload CRC / DTO checksum;
- optionally full DTO bytes.
```

Output comparison:

```text
- event count;
- event type;
- command sequence;
- order id;
- contra order id;
- trade id;
- price ticks;
- quantity lots;
- remaining quantity;
- rejection reason;
- deterministic event ordering;
- optional book state hash.
```

`ExecutionEventWAL` must receive events only after comparator agreement.

---

# Failure reaction

With two lanes there is no majority.

If:

```text
Lane A result != Lane B result
```

then the system does not know which lane is correct.

Correct reaction:

```text
1. Do not append candidate events to ExecutionEventWAL.
2. Stop the affected instrument stream.
3. Mark health as Dead or Corrupted.
4. Record incident diagnostics.
5. Start recovery/replay from trusted durable source.
6. Resume only after trusted state has been reconstructed.
```

Incorrect reactions:

```text
- primary wins;
- choose the result that looks more plausible;
- skip the command;
- continue with one lane;
- write both results;
- silently repair the command.
```

Two lanes provide detection, not correction.

Correction would require an additional trusted source, replica recovery, deterministic replay from known-good state, or more elaborate majority voting. Majority voting still does not solve common-mode software bugs.

---

# Incident diagnostics

On mismatch, the system should record:

```text
- stream id;
- epoch;
- command sequence;
- file offset if available;
- reader A status/error;
- reader B status/error;
- command hash A;
- command hash B;
- command payload A if safe to dump;
- command payload B if safe to dump;
- matcher A candidate events;
- matcher B candidate events;
- book hash A;
- book hash B;
- last committed ExecutionEventWAL position;
- software build/version;
- matching rules version.
```

This diagnostic data is used for replay and operator investigation.

---

# Process isolation variant

The strictest version separates lanes into different processes.

```text
Process A:
    Reader A
    Matcher A

Process B:
    Reader B
    Matcher B

Process C:
    Comparator
    ExecutionEventWAL writer
```

Advantages:

```text
- reduces shared memory corruption risk;
- isolates allocator/object lifetime bugs;
- makes fail-stop behavior cleaner;
- allows different implementations or build options per lane.
```

Costs:

```text
- IPC overhead;
- higher latency;
- more complex deployment;
- more complex diagnostics;
- more difficult backpressure coordination.
```

This is outside prototype scope.

---

# Reference implementation variant

To detect algorithmic bugs, the two lanes should ideally not be identical.

Example:

```text
Lane A:
    optimized production matcher
    price-level maps
    order-id index
    intrusive queues

Lane B:
    simple reference matcher
    slower vectors/lists
    simpler code
    easier to audit
```

If both lanes use the same implementation, the scheme is useful against random corruption in one lane but weaker against deterministic software defects.

For the prototype, a reference matcher is more useful in tests/replay than in the runtime hot path.

---

# Recommended scope decision

## Included in first prototype

```text
- single reader;
- single matcher;
- WAL CRC validation;
- typed reader validation;
- fail-stop on WAL read failure;
- order book invariants;
- deterministic replay;
- replay comparison with ExecutionEventWAL.
```

## Not included in first prototype

```text
- command envelope checksums in hot path;
- online dual reader lanes;
- online dual matcher lanes;
- online comparator before EventWAL commit;
- separate process lanes;
- majority voting;
- production-grade memory corruption mitigation.
```

## Possible later extensions

```text
Phase 2:
    immutable command envelope with checksum;
    reference matcher in tests;
    production matcher vs reference matcher comparison during replay.

Phase 3:
    optional online shadow matcher;
    compare candidate events before ExecutionEventWAL append.

Phase 4:
    dual reader + dual matcher lanes;
    comparator gates execution commit;
    separate process isolation if required.
```

---

# Interview positioning

A concise description:

```text
The prototype deliberately uses one reader and one matcher in the hot path.
It validates WAL records at the reader boundary and relies on deterministic replay
for verification. A stricter high-integrity configuration can duplicate the
read/compute lane: two synchronized WAL readers, two matcher instances, and a
comparator. Execution events are committed only if both lanes read the same
WalPosition, produce the same command envelope, and generate identical candidate
events. This detects silent corruption in one lane but is outside the prototype
scope because it roughly doubles runtime cost and does not eliminate common-mode
failures.
```

---

# Summary

```text
WAL CRC protects stored bytes.
Command envelope checksum protects in-memory handoff.
Dual reader lanes protect the read/decode boundary.
Dual matcher lanes protect computation and mutable order book state.
Comparator protects ExecutionEventWAL from unverified facts.
Replay restores trusted state after fail-stop.
```

The first prototype implements only the basic WAL/replay integrity model.
Dual-lane integrity is documented as a later production-grade extension.
