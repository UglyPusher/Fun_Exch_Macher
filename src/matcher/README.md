# Matcher Directory

This directory is reserved for a future matcher boundary.

No active production code lives here at the current stage. The implemented
matching engine is in `../core/`, where `InstrumentEngine` and `OrderBook` own
the current deterministic matching flow.

Do not add duplicate order-book logic here. Introduce code in this directory
only after the architecture explicitly separates a matcher API from the current
core layer.

See `../../TODO.md` for the open architecture question about whether this
directory should become an active module.
