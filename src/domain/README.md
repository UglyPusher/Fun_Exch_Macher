# Domain Contracts

`src/domain/` owns the durable record and enum contracts shared by matcher, WAL
typed adapters, replay, app, and projections.

The structs in this directory are wire DTOs. They intentionally store enum-like
fields as fixed-width integers because the records are written to WAL as
trivially-copyable binary payloads.

## Owns

- `OrderCommandRecordV1`
- `ExecutionEventRecordV1`
- matching enum numeric values
- WAL record type numeric values

## Does Not Own

- Validation of whether a command is acceptable.
- Order book mutation.
- WAL physical record headers or checksums.
- Projection aggregation rules.

Numeric enum assignments in this directory are durable format. Do not reorder or
reuse values without an explicit migration plan.
