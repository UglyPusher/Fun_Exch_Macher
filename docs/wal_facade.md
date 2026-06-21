# WAL Facade

`wal/wal.hpp` is the public WAL-v0 API.

Normal users of the WAL subsystem should include only this header. The binary
record header, segment header, CRC implementation, padding, concrete segment
reader/writer, and scanner classes are implementation details.

WAL-v0 is feature-frozen. New work should harden tests, documentation, and
recovery behavior around this API rather than adding new binary format features.

## Public API

The facade exposes only the operations needed by current users:

```cpp
wal::WalWriter writer{wal::WalWriterConfig{
    .file_path = "commands.wal",
    .stream_id = 10,
    .epoch = 1,
    .first_sequence = 1
}};

writer.append_record(record_type, payload_bytes);
writer.commit();

wal::WalReader reader{wal::WalReaderConfig{
    .file_path = "commands.wal"
}};

wal::WalRecord record;
reader.read_next_record(record);
```

Recovery helpers:

```cpp
wal::scan_wal_segment(path);
wal::recover_wal_segment(path);
```

`recover_wal_segment()` only truncates a recoverable incomplete trailing record.
It does not repair corrupted complete records.

## Guarantees

WAL-v0 guarantees:

- append assigns stream-local sequence numbers;
- committed records can be read back in sequence;
- record type and payload bytes are preserved;
- readers validate segment header, record header, sequence, payload length, and checksums;
- incomplete trailing records are recoverable crash residue;
- complete invalid records are corruption, including at the physical tail;
- after a writer write/flush failure, the writer is failed and must not be reused.

The facade returns explicit `WalAppendResult`, `WalCommitResult`, `WalReadResult`,
and `WalRecoveryResult` values. Callers must check them.

## Limitations

WAL-v0 intentionally does not implement:

- `fsync` / `fdatasync`;
- segment rotation;
- batch commit metadata;
- compression;
- encryption;
- multi-segment readers;
- partial-write repair on a live writer object.

Current commit semantics:

```text
WalWriter::commit()
    -> WalSegmentWriter::commit()
    -> std::ofstream::flush()
```

This is a C++ stream flush. It is not an operating-system durable commit.

## Recovery Policy

Scanner and recovery behavior:

```text
valid records followed by EOF
    -> ok

valid records followed by incomplete trailing record
    -> recoverable
    -> recover_wal_segment() truncates to the last valid offset

complete record with invalid header, sequence, or CRC
    -> corruption
    -> not recovered by WAL-v0
```

This policy lets a process ignore or truncate a torn crash tail while refusing
to silently accept corrupted complete records.

## Typed Payloads

The facade is raw payload API. Typed DTO conversion belongs at the caller
boundary:

```cpp
const auto payload = std::as_bytes(std::span{&record, 1});
writer.append_record(order_command_record_type, payload);
```

When reading, the caller checks `WalRecord::record_type` and payload size before
copying bytes into a trivially-copyable DTO.

## Demo

Build and run:

```bash
cmake --build build --target wal_demo
./build/wal_demo build/wal_demo.wal
```

The demo writes three records, commits, reads them back, and prints a short
summary. It is intentionally not registered in `ctest`.
