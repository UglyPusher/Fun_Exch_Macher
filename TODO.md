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
