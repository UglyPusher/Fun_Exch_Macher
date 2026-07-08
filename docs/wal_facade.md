# WAL Facade

`wal/wal.hpp` is the normal public WAL-v0 API.

WAL-v0 is a durable append-only committed message queue. Callers append byte
payloads and read only committed payloads back. Segment headers, record
headers, CRC, padding, scanners, raw readers, raw writers, and committed
frontiers are implementation details.

## Public API

Normal users create one `wal::Wal` facade:

```cpp
wal::Wal command_wal{wal::WalConfig{
    .path = "commands.wal",
    .stream_id = 10,
    .epoch = 1,
    .first_sequence = 1
}};

const wal::WalAppendResult append_result = command_wal.append(wal::WalMessageView{
    .record_type = record_type,
    .payload = payload_bytes
});

if (!append_result.ok()) {
    return;
}
```

After successful `append()` the message is written, flushed, synced, committed,
and visible to readers. There is no public `commit()` step.

Batch append commits atomically by visibility:

```cpp
std::array<wal::WalMessageView, 3> messages{first, second, third};
const wal::WalBatchAppendResult result = event_wal.append_batch(messages);
```

If the batch succeeds, all messages are committed. If the write, flush, or sync
fails, none of the batch is published as committed in the current process.

Read with a caller-owned cursor:

```cpp
wal::WalCursor cursor = event_wal.cursor_from_beginning();
wal::WalRecord record;

while (event_wal.read_next(cursor, record).ok()) {
    // record is committed and durable
}
```

Batch read fills up to the provided buffer size:

```cpp
std::array<wal::WalRecord, 128> records;
const wal::WalBatchReadResult result = event_wal.read_batch(cursor, records);
```

## Guarantees

WAL-v0 guarantees:

- `append()` success means durable committed visibility;
- `append_batch()` success means the whole batch is durable and visible;
- readers never return uncommitted messages;
- incomplete trailing records are not exposed as committed data;
- corrupted middle records fail closed instead of being skipped;
- payload length overflow is rejected before writing;
- recovery scans the existing segment and continues the public sequence after
  the last committed message;
- WAL stays payload-agnostic and does not know domain DTO semantics.

## Fixed V0 Policy

WAL-v0 intentionally has no configurable durability policy:

```text
write -> flush -> fsync -> publish committed visibility
```

There is no `NoSync`, `FlushOnly`, `FsyncEveryN`, or virtual commit policy in
this version. If policy variants are needed later, they belong to WAL-v1.

## Recovery Policy

Scanner and recovery behavior:

```text
valid records followed by EOF
    -> committed records are readable

valid records followed by incomplete trailing record
    -> tail is truncated
    -> earlier committed records are readable

complete corrupted record in the middle
    -> WAL opens in corrupted state
    -> reads and appends fail closed
```

Recovery does not silently skip corrupted complete records.

## Typed Payloads

WAL stores bytes. Typed DTO conversion is a thin adapter at the caller
boundary, never a separate WAL implementation.

```cpp
const auto payload = std::as_bytes(std::span{&record, 1});
wal_log.append(wal::WalMessageView{
    .record_type = order_command_record_type,
    .payload = payload
});
```

When reading, callers or typed adapters check `WalRecord::record_type` and
payload size before copying bytes into a trivially-copyable DTO.

## Demo

Build and run:

```bash
cmake --build build --target wal_demo
./build/wal_demo build/wal_demo.wal
```

The demo appends three committed records, reads them back, and prints a short
summary. It is intentionally not registered in `ctest`.
