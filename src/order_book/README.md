# Order Book Directory

This directory is reserved for a future extraction of order-book code.

No active production code lives here at the current stage. The implemented
order book is `../core/include/core/order_book.hpp` and
`../core/src/order_book.cpp`.

Do not add a second order book implementation here. Move code here only as a
deliberate refactor with tests proving that matching behavior and replay event
sequences did not change.
