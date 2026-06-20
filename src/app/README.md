# App Layer

`src/app/` owns the prototype command-line adapter.

It translates demo inputs and user commands into calls across the lower layers:

```text
scenario text
    -> ScenarioLoader
    -> command WAL
    -> InstrumentEngine
    -> event WAL
    -> replay / dump / projection output
```

## Owns

- CLI command routing in `main.cpp`.
- Scenario file parsing.
- Human-readable command and event dumps.
- WAL adapter glue used by the demo executable.

## Does Not Own

- Matching rules.
- Order validation semantics.
- WAL physical format.
- Projection state rules.

`main.cpp` is intentionally still a prototype orchestrator. Split it only when
the extracted boundary has a clear name such as `run_command`, `replay_command`,
or `wal_adapters`.
