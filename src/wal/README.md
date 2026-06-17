# WAL Subsystem

The WAL subsystem is the storage integrity boundary for the matching engine.

It is deliberately small, binary, payload-agnostic, and strict. Its job is not to understand trading commands. Its job is to preserve ordered bytes, prove that those bytes are intact before exposing them, and give upper layers enough metadata to stop, replay, or rebuild streams safely.

```text
Domain DTO
    v typed adapter
Raw WAL API
    v segment reader/writer/scanner
Binary segment file
```

## Why This Exists

The matching engine is intended to be a deterministic reducer:

```text
same command sequence + same matching rules = same execution event sequence
```

That property is only useful if the command/event streams are trustworthy. A corrupted WAL record must never become a domain command or an execution event.

The WAL therefore owns physical integrity:

```text
raw bytes on disk
    -> segment header validation
    -> record header validation
    -> payload checksum validation
    -> sequence validation
    -> validated WalRecordView
    -> typed DTO boundary
    -> matcher / replay / consumers
```

If integrity cannot be proven, the reader fails closed.

## Design Principles

- WAL core is payload-agnostic.
- WAL positions are stream-local, not global.
- `stream_id + epoch + sequence` is the record identity.
- CRC mismatch means "not a valid record".
- Incomplete tail and corrupted middle are different failures.
- Raw reader validates physical integrity before exposing payload bytes.
- Typed reader validates DTO boundary before `memcpy`.
- Domain/matcher code must not repair WAL corruption.
- Recovery is explicit; no silent skip, no heuristic resync.

## Current Scope

Implemented in this prototype:

- binary segment header;
- fixed-size record header;
- payload bytes;
- 8-byte record alignment;
- CRC32 for segment header, record header, and payload;
- stream/epoch/sequence validation;
- raw reader/writer APIs;
- typed read/write adapters for trivially-copyable DTOs;
- segment scanner for recovery decisions;
- writer reopen handling for existing segments;
- truncation of incomplete trailing records on writer reopen;
- refusal to append after middle corruption;
- pending-to-committed writer boundary;
- committed position queue after write/flush acknowledgement;
- tests for corruption, recovery, sequencing, and typed boundaries.

Not implemented yet:

- POSIX `fsync` / `fdatasync` durability policy;
- persisted committed-offset metadata across process restarts;
- multi-segment rotation;
- replicated WAL / quorum append;
- stream health owner;
- matcher integration;
- online dual-lane execution.

## Directory Layout

```text
src/wal/
|-- include/wal/              Public WAL headers
|-- src/                      WAL implementation
|-- doc/                      Integrity and future-design notes
`-- CMakeLists.txt            WAL library target
```

Important headers:

```text
wal_types.hpp                 StreamId, EpochId, SequenceNumber, constants
wal_position.hpp              stream + epoch + sequence
wal_record_header.hpp         fixed record metadata + CRC helper
wal_segment_header.hpp        segment metadata + CRC helper
wal_record_view.hpp           validated raw record view
wal_result.hpp                append/read/commit result types
wal_error.hpp                 physical/typed WAL error codes
raw_wal_writer.hpp            payload-agnostic write interface
raw_wal_reader.hpp            payload-agnostic read interface
typed_wal_writer.hpp          DTO -> bytes adapter
typed_wal_reader.hpp          bytes -> DTO adapter
wal_segment_writer.hpp        binary segment append/reopen logic
wal_segment_reader.hpp        binary segment validated reader
wal_segment_scanner.hpp       recovery scanner
wal_checksum.hpp              CRC32
wal_alignment.hpp             alignment helpers
wal_file.hpp                  filesystem helpers
wal.hpp                       umbrella include
```

## Layer Model

```text
+-----------------------------+
| Domain / Replay / Matcher   |
| understands command meaning |
+--------------^--------------+
               |
+--------------|--------------+
| Typed WAL adapter           |
| validates record type       |
| validates sizeof(TRecord)   |
| memcpy only after raw pass   |
+--------------^--------------+
               |
+--------------|--------------+
| Raw WAL API                 |
| append/read commit results  |
| no domain semantics         |
+--------------^--------------+
               |
+--------------|--------------+
| Segment storage             |
| headers, CRC, alignment     |
| sequence and recovery scan  |
+-----------------------------+
```

The lower layer must never ask, "is this a valid order?" It only asks, "is this a valid WAL record?"

## Record Identity

A WAL position is:

```cpp
struct WalPosition
{
    StreamId stream_id;
    EpochId epoch;
    SequenceNumber sequence;
};
```

There is intentionally no single global sequence across the whole system.

Examples of separate streams:

```text
TradingIngress
InstrumentCommand:EURUSD
InstrumentCommand:BTCUSD
ExecutionEvent:EURUSD
ExecutionEvent:BTCUSD
```

A sequence gap inside one stream epoch is fatal unless an explicit repair or epoch transition explains it.

## Binary File Shape

A segment file is:

```text
+-----------------------+
| WalSegmentHeader      |
+-----------------------+
| WalRecordHeader       |
| payload bytes         |
| zero padding          |
+-----------------------+
| WalRecordHeader       |
| payload bytes         |
| zero padding          |
+-----------------------+
| ...                   |
+-----------------------+
```

Segment header contains:

```text
magic
version
header_size
stream_id
epoch
first_sequence
flags
header_crc
```

Record header contains:

```text
magic
version
header_size
record_length
record_type
stream_id
epoch
sequence
payload_length
payload_crc
header_crc
```

`record_length` is the aligned total size of:

```text
sizeof(WalRecordHeader) + payload_length + padding
```

Current alignment is `DefaultRecordAlignment == 8`.

## Write Path

```text
WalSegmentWriter::append(record_type, payload)
    -> build WalRecordHeader
    -> calculate payload_crc
    -> calculate record header_crc
    -> write header
    -> write payload
    -> write zero padding
    -> advance last_position / next_sequence
```

The writer is payload-agnostic. It does not know whether the bytes represent an order command, execution event, batch, or ingress message.

### Opening a New Segment

```text
file missing or empty
    -> create parent directory
    -> open append stream
    -> write WalSegmentHeader
```

### Reopening an Existing Segment

```text
existing file
    -> read segment header
    -> validate magic/version/static fields
    -> validate segment header CRC
    -> validate stream_id / epoch / first_sequence
    -> scan records
    -> if clean: continue after last valid sequence
    -> if incomplete trailing record: truncate to last valid offset and continue
    -> if middle corruption: fail closed and refuse append
```

This gives restart behavior suitable for the prototype: crash residue at the end can be removed; corruption in the middle cannot be skipped.

## Read Path

```text
WalSegmentReader::read_next(out)
    -> require valid segment header from constructor
    -> read record header
    -> distinguish EOF vs incomplete header
    -> validate static header fields
    -> validate record header CRC
    -> validate stream/epoch
    -> validate expected sequence
    -> read payload
    -> validate payload CRC
    -> skip padding
    -> expose WalRecordView
```

`WalRecordView::payload` points into reader-owned storage. It is valid until the next `read_next()` call on the same reader.

On failure, no payload is exposed as `RecordRead`.

## Scanner And Recovery

`WalSegmentScanner` is used during startup/recovery.

It walks a segment and returns:

```cpp
struct WalSegmentScanResult
{
    bool ok;
    WalError error;
    WalPosition last_valid_position;
    std::uint64_t last_valid_offset;
    bool has_incomplete_trailing_record;
};
```

Scanner rules:

- advance `last_valid_offset` only after full record validation;
- classify incomplete trailing bytes as `IncompleteTrailingRecord`;
- classify middle corruption as `CorruptedMiddleRecord`;
- never resynchronize blindly;
- never mark corrupted bytes as committed data.

Recovery policy in the current writer:

```text
clean segment                 -> append normally
incomplete trailing record    -> truncate tail, append normally
corrupted middle record       -> refuse append
invalid segment header        -> refuse append
wrong stream/epoch            -> refuse append
```

## Typed DTO Boundary

Typed adapters are intentionally thin:

```text
TypedWalWriter<TRecord, RecordType>
    -> static_assert trivially copyable
    -> std::as_bytes(record)
    -> raw_writer.append(type, bytes)

TypedWalReader<TRecord, RecordType>
    -> raw_reader.read_next(view)
    -> validate view.header.record_type
    -> validate view.payload.size() == sizeof(TRecord)
    -> std::memcpy(&record, view.payload.data(), sizeof(TRecord))
```

DTO requirements:

- fixed layout;
- trivially copyable;
- no `std::string`;
- no `std::vector`;
- no pointers;
- no allocator state;
- storage DTO, not business object;
- zero/default initialization where padding could matter.

## Corruption Policy

The key rule is simple:

```text
invalid CRC -> WalReadStatus::Failed -> stream recovery/stop
```

Forbidden behavior:

```text
CRC mismatch -> deserialize anyway
CRC mismatch -> return partial payload
CRC mismatch -> skip record silently
CRC mismatch -> let matcher decide
```

Failure classes:

```text
HeaderChecksumMismatch      header bytes changed
PayloadChecksumMismatch     payload bytes changed
UnexpectedSequence          stream continuity broken
IncompleteTrailingRecord    likely crash residue at file tail
CorruptedMiddleRecord       stream body corrupted; do not skip/resync
InvalidSegmentHeader        segment identity/format cannot be trusted
InvalidRecordHeader         record metadata cannot be trusted
```

Detailed invariants are in:

```text
src/wal/doc/wal_invariants.md
```

## Commit Semantics

Current prototype `commit()` flushes the C++ stream and then publishes pending record positions:

```text
append(record/batch)
    -> binary disk write
    -> pending position

commit()
    -> std::ofstream::flush()
    -> write/flush acknowledgement
    -> committed position queue
```

This is not the same as durable `fsync`.

The code keeps commit policy interfaces because later versions should distinguish:

```text
NoSync      append visible to process, no durability claim
Flush       userspace stream flush
Fsync       OS durable commit via fsync/fdatasync
Replicated  quorum/replica policy
```

Until POSIX fsync support exists, README/docs should treat current commit as prototype flush semantics, not production durability.

## Tests

Run from repository root:

```bash
cmake --workflow --preset test
```

Current WAL test coverage includes:

- record header static validation;
- aligned record length checks;
- CRC32 stability and payload-change detection;
- segment writer append and sequence increment;
- segment writer pending-to-committed publication;
- writer reopen sequence continuation;
- incomplete tail truncation on writer reopen;
- refusal to append after middle corruption;
- segment reader ordered reads and EOF;
- segment header CRC failure;
- record header CRC failure;
- payload CRC failure;
- unexpected sequence failure;
- scanner incomplete-tail classification;
- scanner middle-corruption classification;
- typed writer/reader DTO boundary checks.

## Relationship To Matching Engine

The WAL subsystem does not implement matching.

It supports matching by making these promises:

```text
- accepted records have stable stream-local ordering;
- corrupted records are not exposed as commands/events;
- sequence gaps stop the stream;
- valid bytes can be replayed deterministically;
- derived streams can be scanned and rebuilt by higher layers.
```

The matcher should consume typed records only. If a WAL reader returns `Failed`, the matcher/stream owner must stop the affected stream and trigger recovery policy.

## Future Work

Recommended next steps:

1. POSIX `fsync` / `fdatasync` commit policy.
2. Persisted committed-offset metadata across process restarts.
3. Multi-segment rotation and segment naming.
4. Stream owner / health propagation layer.
5. Recovery API that separates scan, truncate, rebuild, and operator decisions.
6. Reference matcher and deterministic replay harness.
7. Optional command envelope checksum.
8. Optional dual-lane integrity model.

The future dual-lane design is documented in:

```text
src/wal/doc/dual_lane_integrity.md
```

That model is intentionally outside the first prototype runtime path.

## Interview Summary

The WAL is the trust boundary below the deterministic matcher. It stores opaque payloads with stream-local identity, validates segment and record integrity with CRCs, enforces sequence continuity, distinguishes crash-tail residue from middle corruption, and refuses to expose untrusted bytes to typed/domain layers. The matcher receives either a validated typed DTO or a read failure; it never repairs WAL corruption.
