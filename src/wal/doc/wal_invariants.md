# WAL invariants and corruption policy

## Purpose

This document defines non-negotiable invariants for the WAL subsystem.

The WAL is not a passive file format. It is a trust boundary between storage and deterministic domain logic.

For the matching engine, this matters because the matcher is a deterministic reducer:

```text
same command sequence + same matching rules = same execution event sequence
```

Therefore, corrupted WAL records must never become domain commands or domain events.

---

## Layer responsibility

The corruption problem is primarily solved by the WAL reader, not by the matcher.

The matcher must not parse, validate, repair, skip, or reinterpret raw WAL records. It consumes only already validated typed domain records.

However, the matcher still has a defensive responsibility: if its command reader reports a WAL read failure, the matcher must stop the affected instrument stream and must not continue applying commands.

Responsibility split:

```text
Raw WAL reader
    validates physical record integrity
    validates header CRC
    validates payload CRC
    validates record boundaries
    validates sequence continuity if configured

Typed WAL reader
    validates expected record type
    validates payload size
    deserializes only validated payload

Stream owner / runtime
    marks stream health
    applies backpressure
    starts recovery / rebuild / failover

Matcher
    applies only valid typed commands
    stops on reader failure
    never sees corrupted payload
```

---

# Core invariants

## WAL-001: Invalid CRC record is not a record

A WAL record with invalid header CRC or payload CRC is not a valid WAL record.

It must not be exposed to typed readers as payload bytes.

It must not be exposed to domain code as a command, event, or message.

Required behavior:

```text
CRC mismatch -> WalReadStatus::Failed -> stream corruption handling
```

Forbidden behavior:

```text
CRC mismatch -> deserialize anyway
CRC mismatch -> return partial payload
CRC mismatch -> skip record silently
CRC mismatch -> let matcher decide
```

---

## WAL-002: Reader must fail closed

The WAL reader must fail closed.

If record integrity cannot be proven, the reader must return failure.

It is better to stop one instrument than to continue with an untrusted command sequence.

Example:

```cpp
WalReadResult
{
    .status = WalReadStatus::Failed,
    .error = WalError::PayloadChecksumMismatch,
    .position = corrupted_position
};
```

---

## WAL-003: Raw reader owns physical integrity

The raw reader validates:

```text
- segment header magic/version/CRC;
- record header magic/version/static fields;
- record length;
- payload length;
- header CRC;
- payload CRC;
- alignment/padding;
- EOF vs incomplete trailing record;
- monotonic sequence if sequence checking is enabled.
```

The raw reader does not know domain semantics.

It does not know whether payload is an order, trade, cancel, replace, ingress message, or execution event.

---

## WAL-004: Typed reader owns DTO boundary

The typed reader validates:

```text
- record_type == expected record type;
- payload size == sizeof(TRecord);
- TRecord is trivially copyable;
- payload has already passed raw WAL validation.
```

Only after this may it perform `std::memcpy` into a storage DTO.

Forbidden:

```cpp
std::memcpy(&out, payload.data(), sizeof(TRecord)); // before CRC validation
```

Allowed:

```cpp
raw_reader.read_next(view);       // validates physical record
validate_record_type(view);       // validates typed boundary
validate_payload_size(view);      // validates DTO size
std::memcpy(&out, view.payload.data(), sizeof(TRecord));
```

---

## WAL-005: Domain layer must not repair WAL corruption

Domain code must not attempt to repair corrupted WAL payloads.

Forbidden domain behavior:

```text
- infer missing command from next command;
- accept command if fields look plausible;
- clamp invalid price/quantity caused by corruption;
- skip corrupted command and continue;
- synthesize cancel/new/replace command from damaged payload;
- ask matcher to resolve storage corruption.
```

Reason: the matcher is a deterministic reducer, not a storage-repair engine.

---

## WAL-006: Sequence gaps are fatal unless explicitly explained

For a committed stream, command/event sequence must be contiguous inside a stream epoch.

Example valid sequence:

```text
stream=InstrumentCommand:BTCUSD epoch=7 seq=100
stream=InstrumentCommand:BTCUSD epoch=7 seq=101
stream=InstrumentCommand:BTCUSD epoch=7 seq=102
```

Example invalid sequence:

```text
seq=100
seq=102
```

A sequence gap is not equivalent to "no command".

It means the reader does not have a complete deterministic input sequence.

Required behavior:

```text
UnexpectedSequence -> stream stopped -> recovery/rebuild/failover
```

Exception: explicit epoch transition or explicit repair metadata may explain discontinuity.

---

## WAL-007: Tail corruption and middle corruption are different failures

Incomplete trailing record may be crash residue.

Example:

```text
valid record
valid record
half-written record at end of file
```

This is usually recoverable by truncating to the last valid offset, if commit policy permits it.

Middle corruption is different.

Example:

```text
valid record
corrupted record
valid-looking later bytes
```

This must be treated as stream corruption. The reader must not resynchronize blindly and continue.

Required classification:

```text
IncompleteTrailingRecord -> truncate/recover tail if allowed
CorruptedMiddleRecord    -> stream corrupted, require rebuild/replica/operator
```

---

## WAL-008: Derived streams may be rebuilt

A derived WAL stream may be deleted, truncated, or rebuilt if its source-of-truth stream is intact.

In this architecture:

```text
TradingIngressWAL        = durable external truth
InstrumentCommandWAL     = normalized deterministic derived stream
ExecutionEventWAL        = durable matching truth, but regenerable from commands for verification
```

If `InstrumentCommandWAL` is corrupted and `TradingIngressWAL` is valid:

```text
1. stop affected instrument stream;
2. mark CommandWAL corrupted;
3. rebuild commands from TradingIngressWAL;
4. replay matcher;
5. compare generated execution events with existing ExecutionEventWAL;
6. resume only after exact verification.
```

---

## WAL-009: Source-of-truth corruption requires replica or operator intervention

If a source-of-truth WAL is corrupted, the system must not invent missing facts.

For `TradingIngressWAL` corruption:

```text
- recover from replica;
- recover from quorum copy;
- recover from upstream archive if available;
- otherwise stop affected scope for operator repair.
```

Prototype policy:

```text
TradingIngressWAL CRC mismatch = fatal recovery error.
```

---

## WAL-010: Visibility only after validated commit

A WAL record becomes visible to readers only after it is committed according to the stream commit policy.

Pending records are not domain facts.

If the process crashes before commit, pending records may be lost and regenerated during replay.

Required behavior:

```text
append pending -> not visible
commit ok      -> visible
commit failed  -> not visible
```

---

## WAL-011: Matcher must stop on reader failure

Even though corruption is detected by the WAL reader, the matcher must treat read failure as fatal for the affected instrument stream.

Example:

```cpp
auto read_result = command_reader.read_next(command);

if (read_result.status != wal::WalReadStatus::RecordRead)
{
    return MatcherStepResult
    {
        .status = MatcherStepStatus::InputStreamFailed,
        .error = read_result.error,
        .position = read_result.position
    };
}

order_book.apply(command);
```

The matcher must not do this:

```cpp
if (crc_failed)
{
    continue; // forbidden
}
```

---

# CRC mismatch policy

## Case 1: CRC mismatch in InstrumentCommandWAL

This is corruption of a derived deterministic stream.

System reaction:

```text
Raw reader detects CRC mismatch
    -> read failure
    -> typed reader does not deserialize
    -> matcher receives no command
    -> stream owner marks instrument Dead
    -> ingress rejects/throttles new commands for this instrument
    -> recovery rebuilds CommandWAL from TradingIngressWAL
```

Required state transition:

```text
Instrument health: Alive/Degraded -> Dead
Reason: CorruptedCommandStream
Recovery action: RebuildFromIngressWAL
```

The matcher must not continue after the corrupted command.

Reason: skipping one command changes the order book and invalidates all following matching results.

---

## Case 2: CRC mismatch in TradingIngressWAL

This is corruption of durable external truth.

System reaction:

```text
Raw reader detects CRC mismatch
    -> read failure
    -> source-of-truth stream marked corrupted
    -> affected recovery/replay path stops
    -> affected admission scope stops
    -> replica recovery or operator intervention required
```

Prototype policy:

```text
FatalError: IngressWALCorrupted
```

---

## Case 3: CRC mismatch in ExecutionEventWAL

This is corruption of durable matching output.

If command source is valid, execution events may be regenerated and compared.

System reaction:

```text
Raw reader detects CRC mismatch
    -> downstream consumers stop at last verified event
    -> replay commands
    -> regenerate execution events
    -> compare prefix before corruption
    -> rebuild/replace corrupted event-log tail if exact match is proven
```

Forbidden:

```text
market-data/DB/consumer continues after corrupted event
```

---

# Retry policy

A single controlled retry is allowed for diagnostic purposes.

Example:

```text
1. read record;
2. CRC mismatch;
3. close/reopen file or reread same offset;
4. if CRC is valid after reread -> continue and log transient read incident;
5. if CRC mismatch repeats -> stream corruption.
```

Retry must not change domain semantics.

Retry must not become an infinite loop.

Retry must not let corrupted payload reach typed/domain layers.

---

# Health propagation

Corruption state propagates upward.

For per-instrument command stream corruption:

```text
InstrumentCommandWAL: Dead
    -> Matcher: Dead
    -> CommandStream: Dead
    -> Normalizer route for instrument: Dying/Dead
    -> Trading ingress admission for instrument: reject/throttle
```

For global storage corruption:

```text
Storage: Dead
    -> all WAL streams Dead
    -> all matchers Dead
    -> global trading admission rejected
```

Health states:

```text
Alive -> Degraded -> Dying -> Dead
```

Suggested admission behavior:

```text
Alive:
    accept NewOrder, CancelOrder, ReplaceOrder

Degraded:
    accept trading commands, possibly throttle

Dying:
    reject/throttle NewOrder
    probably reject ReplaceOrder
    allow CancelOrder only if commit path is still guaranteed

Dead:
    reject trading commands
    allow only recovery/admin commands
```

---

# Reader API expectations

## Raw reader

The raw reader may expose records only after physical validation.

Suggested API:

```cpp
class RawWalReader
{
public:
    virtual ~RawWalReader() = default;

    virtual WalReadResult read_next(WalRecordView& out) = 0;

    [[nodiscard]] virtual WalPosition last_position() const noexcept = 0;
};
```

On CRC failure:

```cpp
return WalReadResult
{
    .status = WalReadStatus::Failed,
    .error = WalError::PayloadChecksumMismatch,
    .position = corrupted_position
};
```

On incomplete tail:

```cpp
return WalReadResult
{
    .status = WalReadStatus::Failed,
    .error = WalError::IncompleteTrailingRecord,
    .position = last_valid_position
};
```

---

## Typed reader

The typed reader must not receive unvalidated payloads.

Suggested behavior:

```cpp
WalReadResult read_next(TRecord& out)
{
    WalRecordView view;

    auto result = raw_reader_.read_next(view);
    if (result.status != WalReadStatus::RecordRead)
    {
        return result;
    }

    if (view.header.record_type != TExpectedRecordType)
    {
        return failed(WalError::RecordTypeMismatch, result.position);
    }

    if (view.payload.size() != sizeof(TRecord))
    {
        return failed(WalError::PayloadSizeMismatch, result.position);
    }

    std::memcpy(&out, view.payload.data(), sizeof(TRecord));
    return result;
}
```

---

# Scanner invariants

The segment scanner is used during startup/recovery.

It must find the last safe recovery point.

Required output:

```cpp
struct WalSegmentScanResult
{
    bool ok = false;
    WalError error = WalError::None;

    WalPosition last_valid_position {};
    std::uint64_t last_valid_offset = 0;

    bool has_incomplete_trailing_record = false;
};
```

Scanner rules:

```text
- accept only fully validated records;
- advance last_valid_offset only after full record validation;
- classify incomplete tail separately from middle corruption;
- never resynchronize silently after middle corruption;
- never mark corrupted bytes as committed data.
```

---

# Recovery policy table

| Stream | Corruption class | Automatic recovery | Required action |
|---|---:|---:|---|
| TradingIngressWAL | CRC/header/sequence corruption | Only from replica/archive | Stop affected scope; recover from replica or operator repair |
| InstrumentCommandWAL | CRC/header/sequence corruption | Yes, if IngressWAL is valid | Rebuild derived command stream from ingress |
| ExecutionEventWAL | CRC/header/sequence corruption | Yes, if commands are valid and comparison succeeds | Regenerate/verify/rebuild event log tail |
| In-memory CommandStream | Loss/corruption | Yes | Rebuild from WAL source |
| Checkpoint | CRC/version mismatch | Yes | Discard checkpoint and replay from earlier point |

---

# Prototype policy

For the first prototype, keep the policy strict and simple:

```text
1. Raw reader validates CRC before exposing payload.
2. Typed reader validates record type and payload size.
3. Matcher never sees corrupted commands.
4. Any CommandWAL corruption stops the instrument.
5. CommandWAL may be rebuilt from IngressWAL.
6. IngressWAL corruption is fatal.
7. ExecutionEventWAL corruption requires replay and exact comparison.
8. No silent skip.
9. No heuristic repair.
10. No best-effort matching after corruption.
```

---

# Interview-grade summary

A concise explanation:

```text
CRC mismatch is handled below the matcher. The raw WAL reader treats an invalid CRC as storage corruption and does not expose the payload. The typed reader therefore never deserializes corrupted bytes. The matcher receives either a valid typed command or a read failure. On read failure it stops the affected instrument stream. If the corrupted stream is derived, such as InstrumentCommandWAL, it is rebuilt from the durable TradingIngressWAL. If the corrupted stream is a source-of-truth log, recovery requires a replica, archive, or operator repair.
```
