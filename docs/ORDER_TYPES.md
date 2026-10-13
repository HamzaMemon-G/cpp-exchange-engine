# Order types and amendment priority

The simulator has one instrument and one owner thread. Prices are positive
integer ticks; quantities are positive signed 64-bit integers. All trades use
the resting maker's price, best price first and FIFO within that price level.

| Command | Matches | Unfilled quantity |
|---|---|---|
| `ADD id BUY/SELL price quantity` | At the limit or better | Rests until filled, cancelled or amended (GTC) |
| `IOC id BUY/SELL price quantity` | At the limit or better | Expires immediately |
| `MARKET id BUY/SELL quantity` | Across all available opposite prices | Expires immediately |
| `AMEND id price remaining_quantity` | May match if the new price crosses | Rests at the new price |

An accepted IOC or market order with no available match is still `ACCEPTED`.
The CLI prints `EXPIRED sequence=N id=I quantity=Q` when quantity expires.
The C++ `Result::expired_quantity` contains that quantity, or zero. Expiration
does not consume a separate sequence number and is not a business rejection.
An accepted order's ID remains reserved even if its entire quantity expires.
Such an order cannot subsequently be cancelled or amended.

Market matching uses an explicit no-limit flag rather than a sentinel price.
It can consume makers at any valid tick, including 1 and 9223372036854775807.
No price protection, risk checks, fees, routing or real exchange connection is
implied. These are simulated immediate orders, with no overnight or session policy.

## Amendments

`AMEND` changes an active resting GTC order. The ID and side stay fixed; the
quantity argument is the **new remaining quantity**, not the original total or
an increment. After an original quantity of 10 has filled by 4, `AMEND 1 100 8`
sets remaining quantity to 8, adding 2 to the previous remaining quantity of 6.

| Change | FIFO priority | Book order sequence |
|---|---|---|
| Same price, smaller quantity | Retained | Original priority sequence |
| Same price, same quantity (no-op) | Retained | Original priority sequence |
| Same price, larger quantity | Moves to the back | Amendment command sequence |
| Any price change, including with a reduction | Removed, matched, then remainder queued | Amendment command sequence |

Every amendment attempt consumes one command sequence, including a no-op or
rejection. A retained-priority order therefore has an older book sequence than
the latest command. A changed-price amendment can consume several makers;
all resulting trades share the amendment command sequence and use the same ID
as taker. A fully filled amendment leaves no active order, with its ID reserved.

Zero/negative price or remaining quantity is invalid, without changing the order.
Use `CANCEL id` to remove all remaining quantity. Filled, cancelled, expired or
never-seen IDs return `UNKNOWN_ID` for amendments; an amendment cannot resurrect
one. There is no side change, IOC amendment, FOK or replace-with-new-ID operation.

The C++ API is `add(id, side, price, quantity, TimeInForce::Gtc)` (the fifth
argument defaults to GTC), `add(..., TimeInForce::Ioc)`, `market(id, side, quantity)`
and `amend(id, price, remaining_quantity)`. Invalid values, side and time-in-force
enums return `INVALID_ORDER`; validation precedes ID reservation or book mutation.
Allocation failure remains fatal, as documented in the README.

Run the complete [example](../examples/order-types.txt):

```sh
./build/exchange_cli --input examples/order-types.txt
```

Its [expected output](../examples/order-types-output.txt) is checked byte for byte
by CLI integration tests. The example shows order 1 keeping its position after
reduction, moving behind order 2 after an increase, an IOC expiration, and an
amendment trading at the maker's price before resting its remainder.

## Versioned journals

New journals use `EXCHANGE_LOG_V2` and record canonical ADD, CANCEL, IOC, MARKET
and AMEND attempts. Expiration and amendment priority are recomputed by replay;
there are no extra expiration records. V1 journals remain replayable and can
still append ADD/CANCEL. They refuse new command types before writing a record.
Both formats retain parser/canonical/sequence/truncation checks.

To use new commands with an old journal, stop its writer and upgrade a copy:

```sh
./build/exchange_cli --upgrade-log old.log --output-log upgraded.log
./build/exchange_cli --replay upgraded.log
./build/exchange_cli --log upgraded.log
```

The destination must not exist. The source is fully replay-validated before a
copy is created, its header is changed to V2, and the copy is validated again.
The source is unchanged; sequences, book and reserved IDs carry over exactly.
Already-V2 sources can also be copied through the same operation. This is an
offline operation with one writer, not an atomic crash-safe migration protocol.
Do not modify the source during copying or use a destination until the operation
succeeds. Stream flush does not promise device persistence. Preserve the source
if copying is interrupted; an incomplete output will fail replay validation.
