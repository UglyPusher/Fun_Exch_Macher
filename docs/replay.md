# Replay

## 1. Purpose

This document defines the replay model for the matching engine prototype.

Replay is a core architectural feature. It is not only a debugging tool.

Replay is used to verify that the matching engine is deterministic:

```text
same command sequence + same matching rules = same execution event sequence
```

If replay produces different events from the original run, the system has detected one of the following problems:

* corrupted command log;
* corrupted execution event log;
* changed matching rules;
* non-deterministic matcher behavior;
* invalid checkpoint;
* inconsistent event comparison rules;
* implementation regression.

---

## 2. Core Idea

The matching engine is modelled as a deterministic transformation:

```text
Command WAL -> Matcher -> Execution Event WAL
```

During replay, the engine rebuilds state from the command log and regenerates execution events.

The regenerated events are compared with the stored execution event log.

```text
Stored Command WAL
        |
        v
Replay Matcher
        |
        v
Generated Execution Events
        |
        v
Compare with Stored Execution Event WAL
```

Replay proves that the engine can reconstruct its observable output from durable input.

---

## 3. What Replay Validates

Replay validates:

* command ordering;
* matching determinism;
* event sequencing;
* order book reconstruction;
* event log consistency;
* invariant preservation;
* checkpoint correctness, if checkpoints are used.

Replay does not validate:

* external network delivery;
* client authentication;
* external protocol parsing;
* risk checks outside the matcher;
* database mirror correctness;
* market data feed correctness;
* production durability guarantees.

Those may be tested separately.

---

## 4. Replay Inputs

Replay requires:

```text
Command WAL
Matching rules version
Initial state or checkpoint
Optional stored Execution Event WAL
```

The simplest replay starts from an empty order book and reads the Command WAL from the beginning.

```text
empty OrderBook + Command WAL = reconstructed state + regenerated events
```

---

## 5. Replay Outputs

Replay may produce:

```text
Reconstructed order book
Generated execution events
Replay validation report
Mismatch diagnostics
Invariant violations
```

A replay run should be able to answer:

```text
Was the command stream processed deterministically?
At which command did the first mismatch occur?
What was expected?
What was generated?
What was the book state before and after the command?
```

---

## 6. Basic Replay Procedure

The minimal replay procedure:

```text
1. Create an empty order book.
2. Open the Command WAL.
3. Read commands in sequence order.
4. Apply each command to the replay matcher.
5. Generate execution events.
6. Compare generated events with stored events.
7. Verify order book invariants after every command.
8. Report success or the first mismatch.
```

Pseudo-code:

```cpp
ReplayResult replay(
    CommandLogReader& command_reader,
    EventLogReader& stored_event_reader,
    Matcher& matcher,
    EventComparator& comparator)
{
    ReplayResult result;

    while (command_reader.read_next_command(command))
    {
        auto generated_events = matcher.apply(command);

        for (const auto& generated_event : generated_events)
        {
            ExecutionEvent stored_event;

            if (!stored_event_reader.read_next_event(stored_event))
            {
                result.fail("Stored event log ended too early", command.sequence);
                return result;
            }

            if (!comparator.equal(generated_event, stored_event))
            {
                result.fail("Execution event mismatch", command.sequence);
                result.expected = stored_event;
                result.actual = generated_event;
                return result;
            }
        }

        if (!matcher.order_book().check_invariants())
        {
            result.fail("Order book invariant violation", command.sequence);
            return result;
        }
    }

    if (stored_event_reader.has_more_events())
    {
        result.fail("Stored event log contains extra events");
        return result;
    }

    result.success();
    return result;
}
```

The exact C++ interface may differ. The important property is the same: replay consumes commands, regenerates events, and compares them deterministically.

---

## 7. Replay Modes

## 7.1 Validation Replay

Validation replay compares generated events against the stored Execution Event WAL.

Purpose:

```text
prove that stored command input reproduces stored event output
```

This is the strongest replay mode for testing determinism.

## 7.2 State Reconstruction Replay

State reconstruction replay rebuilds the order book state from the Command WAL without comparing events.

Purpose:

```text
recover current book state
inspect final state
debug command history
```

This is useful when the event log is unavailable or not needed.

## 7.3 Event Regeneration Replay

Event regeneration replay reads the Command WAL and writes a new Execution Event WAL.

Purpose:

```text
regenerate event log after intentional event format changes
build derived outputs
test new event consumers
```

This mode must be used carefully. If matching rules changed, regenerated events may not match historical events.

## 7.4 Checkpoint Replay

Checkpoint replay starts from a previously saved order book snapshot.

Purpose:

```text
reduce recovery time
avoid replaying the entire command history
```

A checkpoint replay must still preserve deterministic behavior.

---

## 8. Event Comparison

Replay comparison must not compare formatted strings.

It must compare normalized event fields.

Required comparison fields:

```text
event type
event sequence
command sequence
instrument id
order id
contra order id
side, where applicable
price
quantity
remaining quantity
trade id
rejection reason
event order
```

Optional fields must be classified carefully.

For example, wall-clock timestamps should not affect replay comparison unless they are part of the input command stream and deterministic output contract.

---

## 9. Event Sequence Rules

A single command may produce multiple events.

Example:

```text
Command #42: NewOrder Buy 101.00 qty 10
Event #100: OrderAccepted, command #42
Event #101: TradeExecuted, command #42
Event #102: TradeExecuted, command #42
Event #103: OrderFullyFilled, command #42
```

Replay must preserve:

```text
number of events per command
event order inside command processing
event fields
global event order
```

If the original run produced four events for a command and replay produces three, replay must fail.

If replay produces the same events in a different order, replay must fail.

---

## 10. Determinism Requirements

The matcher must not depend on non-deterministic inputs.

Forbidden inside deterministic matching logic:

```text
wall-clock time
random numbers
thread scheduling
network state
database state
unordered container iteration affecting output
mutable global state
external service calls
```

Allowed:

```text
command sequence
event sequence from deterministic event writer
timestamps already present in command input
instrument-local trade sequence
explicit configuration version
matching rules version
```

If time is required, it must be part of the command stream.

Bad:

```cpp
event.timestamp = system_clock::now();
```

Acceptable:

```cpp
event.accepted_timestamp = command.accepted_timestamp;
```

---

## 11. Command Log Requirements

Replay assumes that the Command WAL is ordered and readable.

Minimum command log requirements:

```text
records can be read sequentially
record order is stable
command sequence is explicit
malformed records are detected
incomplete trailing records are detected
payload can be decoded deterministically
```

If command sequence numbers are not contiguous, replay policy must define whether this is valid.

Recommended first prototype policy:

```text
command sequence numbers must be strictly increasing and contiguous inside one Command WAL
```

This makes debugging easier.

---

## 12. Execution Event Log Requirements

The stored Execution Event WAL must be readable in event sequence order.

Minimum event log requirements:

```text
events can be read sequentially
event sequence is explicit
command sequence reference is present
malformed records are detected
incomplete trailing records are detected
payload can be decoded deterministically
```

Recommended first prototype policy:

```text
event sequence numbers must be strictly increasing and contiguous inside one Execution Event WAL
```

---

## 13. Replay Failure Classes

Replay failures should be classified explicitly.

## 13.1 Command Log Failure

Examples:

```text
cannot open command log
invalid command record
unexpected command sequence
malformed payload
checksum mismatch
corruption in the middle of the log
```

## 13.2 Event Log Failure

Examples:

```text
cannot open event log
invalid event record
unexpected event sequence
malformed payload
checksum mismatch
stored event log ended too early
stored event log contains extra events
```

## 13.3 Matching Failure

Examples:

```text
matcher rejected a command differently
trade quantity differs
execution price differs
event count differs
event order differs
rejection reason differs
trade id differs
```

## 13.4 Invariant Failure

Examples:

```text
best bid crosses best ask after matching completes
active order index differs from price levels
order has non-positive remaining quantity
empty price level remains in book
FIFO order is broken
```

## 13.5 Checkpoint Failure

Examples:

```text
checkpoint version is unsupported
checkpoint instrument does not match replay stream
checkpoint last command sequence is invalid
checkpoint checksum mismatch
replay from checkpoint diverges from full replay
```

---

## 14. Mismatch Diagnostics

A replay mismatch must produce enough information to debug the issue.

Minimum diagnostic output:

```text
command sequence
command payload
expected event
actual event
event index within command
global event sequence
book state before command
book state after command, if available
failure class
```

Example:

```text
Replay failed

Command sequence: 42
Command: NewOrder Buy 101.00 qty 10 order B1

Mismatch at event 2 for command 42

Expected:
TradeExecuted trade_id=7 price=100.75 qty=4 resting=S3 incoming=B1

Actual:
TradeExecuted trade_id=7 price=101.00 qty=4 resting=S3 incoming=B1

Failure class:
MatchingFailure.ExecutionPriceMismatch
```

The first mismatch is usually more useful than a long list of cascading differences.

---

## 15. Book State Snapshots for Debugging

Replay diagnostics may include compact book snapshots.

Example:

```text
Before command #42:

Bids:
100.75: B7 qty 4
100.50: B3 qty 10

Asks:
101.00: S9 qty 5
101.25: S2 qty 8
```

After command #42:

```text
Bids:
100.75: B7 qty 4
100.50: B3 qty 10

Asks:
101.25: S2 qty 8
```

This should be used for debugging and tests, not as a replacement for deterministic event comparison.

---

## 16. Checkpoint Replay

A checkpoint is an optimization for faster recovery.

A checkpoint contains a serialized order book state and metadata.

Minimum checkpoint metadata:

```text
checkpoint version
instrument id
last applied command sequence
last generated event sequence
last generated trade id
matching rules version
book snapshot checksum
```

Checkpoint replay procedure:

```text
1. Load checkpoint.
2. Validate checkpoint metadata.
3. Restore order book state.
4. Open Command WAL.
5. Seek to command after last_applied_command_sequence.
6. Replay remaining commands.
7. Continue event generation from last_generated_event_sequence + 1.
8. Continue trade id generation from last_generated_trade_id + 1.
```

Important rule:

```text
A checkpoint must never replace the Command WAL as the source of truth.
```

It only reduces replay time.

---

## 17. Full Replay vs Checkpoint Replay

Checkpoint replay must be validated against full replay.

For a given command stream:

```text
full replay from empty state
```

and:

```text
checkpoint replay from command N
```

must produce the same final book state and event sequence after command N.

Test procedure:

```text
1. Run full replay to command N.
2. Save checkpoint.
3. Continue full replay to the end.
4. Load checkpoint.
5. Replay from N + 1 to the end.
6. Compare final book states.
7. Compare generated events after checkpoint.
```

---

## 18. Matching Rules Version

Replay depends on matching rules.

If matching rules change, replay may intentionally produce different events.

Therefore, logs or replay configuration should identify the matching rules version.

Minimum metadata:

```text
matching_rules_version
event_schema_version
command_schema_version
```

For the first prototype, this can be simple constants.

Example:

```text
matching_rules_version = "prototype-v1"
command_schema_version = "command-v1"
event_schema_version = "event-v1"
```

If a replay run uses a different matching rules version from the original event log, it must report this explicitly.

---

## 19. Schema Versioning

Replay also depends on command and event schemas.

Schema changes must be handled explicitly.

Possible policies:

```text
reject unsupported version
decode old version into current internal DTO
run replay with historical schema adapter
```

For the first prototype:

```text
reject unsupported version
```

is acceptable.

This keeps the replay model simple.

---

## 20. Handling Timestamps

Timestamps must not break replay.

Recommended policy:

```text
Timestamps assigned before Command WAL entry may be replayed as command fields.
Timestamps generated inside matcher are forbidden for deterministic events.
Timestamps generated by downstream consumers are outside replay comparison.
```

If an execution event includes a timestamp, it must either:

```text
come from the input command
```

or:

```text
be excluded from deterministic comparison
```

The first prototype should avoid event timestamps in the matching core.

---

## 21. Handling IDs

ID generation must be deterministic.

## 21.1 Event IDs

Event sequence should be assigned by the event log writer or deterministic event sequencer.

During validation replay, generated event sequence must match stored event sequence.

## 21.2 Trade IDs

Recommended first prototype policy:

```text
trade_id is monotonically increasing per instrument
```

Replay must restore or recompute trade IDs deterministically.

If replay starts from a checkpoint, the checkpoint must store the last generated trade ID.

## 21.3 Order IDs

Order IDs are part of input commands.

The matcher must not create arbitrary order IDs for client orders.

---

## 22. Replay and Multiple Instruments

For the first prototype, one instrument is enough.

For multiple instruments, replay can be done in two ways.

## 22.1 Global Command Replay

A single global Command WAL is replayed in original order.

Commands are dispatched to instrument matchers.

Advantages:

```text
preserves global command acceptance order
simple audit model
```

Disadvantages:

```text
more complex event ordering across instruments
more work if replaying only one instrument
```

## 22.2 Per-Instrument Replay

Each instrument has its own Command WAL.

Advantages:

```text
simple deterministic replay per order book
natural matcher ownership
faster isolated recovery
```

Disadvantages:

```text
requires dispatcher
cross-instrument global ordering is not directly represented
```

Preferred long-term architecture:

```text
Ingress WAL -> Per-Instrument Command WALs -> Per-Instrument Execution Event WALs
```

For the first prototype, use one instrument or one logical command stream.

---

## 23. Replay and Consumers

Replay validates the matching core.

Downstream consumers should be tested separately.

Examples of consumers:

```text
market data builder
trade report builder
database mirror
portfolio projection
audit export
```

A consumer can replay the Execution Event WAL to build its own state.

This is a separate replay model:

```text
Execution Event WAL -> Consumer State
```

Do not mix matcher replay with consumer replay in the first prototype.

Current prototype status:

```text
Implemented:
- MarketDataProjection consumes ExecutionEventRecordV1 only
- public book depth is rebuilt from event stream
- trade tape is rebuilt from TradeExecuted events
- consumer projection rejects event sequence gaps

Not implemented:
- consumer checkpoints
- market data protocol/feed serialization
```

---

## 24. Replay Test Cases

Required replay tests:

```text
empty log replay
single passive buy
single passive sell
aggressive buy full fill
aggressive sell full fill
partial fill
multi-level sweep
FIFO priority
cancel existing order
cancel unknown order
duplicate order rejection
invalid price rejection
invalid quantity rejection
event log ends too early
event log contains extra events
event field mismatch
```

Optional later tests:

```text
checkpoint replay
multiple instruments
JSONL malformed command
JSONL malformed event
binary WAL checksum mismatch
incomplete trailing record
schema version mismatch
matching rules version mismatch
```

---

## 25. Example Replay Session

Command WAL:

```json
{"seq":1,"type":"NewOrder","instrument":"TEST","order_id":100,"client_id":1,"side":"Sell","price":10100,"qty":5}
{"seq":2,"type":"NewOrder","instrument":"TEST","order_id":101,"client_id":2,"side":"Buy","price":10100,"qty":5}
```

Stored Execution Event WAL:

```json
{"seq":1,"cmd_seq":1,"type":"OrderAccepted","instrument":"TEST","order_id":100,"price":10100,"qty":5}
{"seq":2,"cmd_seq":2,"type":"OrderAccepted","instrument":"TEST","order_id":101,"price":10100,"qty":5}
{"seq":3,"cmd_seq":2,"type":"TradeExecuted","instrument":"TEST","trade_id":1,"incoming_order_id":101,"resting_order_id":100,"price":10100,"qty":5}
{"seq":4,"cmd_seq":2,"type":"OrderFullyFilled","instrument":"TEST","order_id":100}
{"seq":5,"cmd_seq":2,"type":"OrderFullyFilled","instrument":"TEST","order_id":101}
```

Replay result:

```text
Replay OK
Commands read: 2
Events compared: 5
Final book: empty
Invariant checks: passed
```

---

## 26. Example Mismatch

Command WAL:

```json
{"seq":1,"type":"NewOrder","instrument":"TEST","order_id":100,"client_id":1,"side":"Sell","price":10100,"qty":5}
{"seq":2,"type":"NewOrder","instrument":"TEST","order_id":101,"client_id":2,"side":"Buy","price":10150,"qty":5}
```

Stored event:

```json
{"seq":3,"cmd_seq":2,"type":"TradeExecuted","instrument":"TEST","trade_id":1,"incoming_order_id":101,"resting_order_id":100,"price":10150,"qty":5}
```

Generated event:

```json
{"seq":3,"cmd_seq":2,"type":"TradeExecuted","instrument":"TEST","trade_id":1,"incoming_order_id":101,"resting_order_id":100,"price":10100,"qty":5}
```

Replay result:

```text
Replay failed

Failure class:
MatchingFailure.ExecutionPriceMismatch

Command sequence:
2

Expected price:
10150

Actual price:
10100

Reason:
The prototype executes trades at resting order price.
```

This kind of mismatch is useful because it immediately identifies either a stored event error or a matching rule mismatch.

---

## 27. Minimal Acceptance Criteria

Replay is acceptable for the first prototype when:

```text
Command WAL can be replayed from the beginning.
Generated events can be compared with stored events.
Replay detects event count mismatch.
Replay detects event field mismatch.
Replay detects event order mismatch.
Replay detects extra stored events.
Replay detects missing stored events.
Replay verifies order book invariants after every command.
Replay reports the first mismatch with command sequence and event details.
Replay does not depend on network, database, or wall-clock time.
```

Current prototype status:

```text
Implemented:
- validation replay from command reader and stored event reader
- regenerated event comparison through normalized fields
- first mismatch diagnostics with failure class, command sequence, event index, expected/actual event, and book snapshots
- command sequence break detection
- extra stored event detection
- missing stored event detection
- event field and event order mismatch detection
- order book invariant checks after each replayed command
- replay tests for NewOrder, CancelOrder, and ReplaceOrder paths

Not implemented:
- checkpoint replay
- multi-segment rotated replay
- schema-version adapters
```

---

## 28. Summary

Replay is the mechanism that makes the prototype more than a simple order book demo.

The central rule is:

```text
The command log is durable input.
The matcher is deterministic logic.
The execution event log is durable output.
Replay proves the relationship between them.
```

A matching engine without replay is just mutable state.

A replayable matching engine has an auditable, testable, and recoverable core.
