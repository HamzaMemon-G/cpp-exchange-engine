# C++ Exchange Matching Engine

A standalone, CPU-only, single-instrument limit-order matching engine in C++20.
Includes a scriptable CLI, an append-only command journal, deterministic replay,
an independent reference-model test, and a reproducible synthetic benchmark.
No accounts, credentials, market data, networking, or external dependencies.

## Build and test

Requires CMake 3.20+, a C++20 compiler, and its build tools. No packages need to
be downloaded by the build. All tests use explicit checks, including in Release.

Linux (GCC or Clang):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/exchange_cli --input examples/commands.txt
./build/exchange_bench --orders 100000 --seed 42 --samples 5 --latency
```

Windows with MinGW GCC and Ninja on PATH (PowerShell):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
.\build\exchange_cli.exe --input examples/commands.txt
.\build\exchange_bench.exe --orders 100000 --seed 42 --samples 5 --latency
```

Windows with Visual Studio C++ tools installed:

```powershell
cmake -S . -B build
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
.\build\Release\exchange_cli.exe --input examples/commands.txt
```

Use a separate directory for each compiler/configuration. Linux sanitizer checks:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DEXCHANGE_SANITIZERS=ON
cmake --build build-sanitize --parallel 2
ctest --test-dir build-sanitize --output-on-failure
```

Sanitizers need compiler/runtime support; this option is intended for GCC/Clang
on Linux. The CI configuration exercises GCC and Clang with sanitizers on Linux,
and MSVC Release on Windows. See [validation](docs/VALIDATION.md) for what was
actually verified locally; adding a workflow does not establish that CI passed.

## Commands

One command per line, case sensitive, whitespace separated:

| Command | Meaning |
|---|---|
| `ADD <id> BUY <price> <quantity>` | Submit buy limit order |
| `ADD <id> SELL <price> <quantity>` | Submit sell limit order |
| `CANCEL <id>` | Cancel all remaining quantity of an active order |
| `BOOK` | Print each resting order in price-time order |
| `QUIT` | Stop; subsequent input is ignored |

Blank lines and whole-line `#` comments are ignored; inline comments are not
supported. Numeric fields are decimal integers, without a sign. IDs are
1..18446744073709551615; prices and quantities are 1..9223372036854775807.
Prices are abstract ticks; no currency or fractional-price assumptions are made.
Both LF and CRLF command files are accepted.

Without `--input`, commands come from stdin; an EOF ends the session.
`--help` prints usage. Order results and trades go to stdout, parser errors to
stderr. Exit codes: 0 = all commands accepted; 2 = at least one parser/business
rejection (processing continued); 1 = fatal option/file/log/runtime error.
Rejects are deliberate in generated workloads, so their CLI runs usually exit 2.

The sample's first crossing order produces:

```text
RESULT sequence=4 id=4 status=ACCEPTED
TRADE sequence=4 maker=1 taker=4 price=100 quantity=10
TRADE sequence=4 maker=2 taker=4 price=100 quantity=2
BOOK sequence=4
SELL price=100 remaining=3 id=2 sequence=2
SELL price=101 remaining=8 id=3 sequence=3
END_BOOK
```

## Matching rules and boundaries

* Best price first, FIFO within a price level. A buy crosses at ask <= limit;
  a sell crosses at bid >= limit. Execution always uses the resting order price.
* One incoming order may consume several makers and price levels. A partially
  filled maker stays at the front of its existing queue. An unfilled incoming
  remainder rests at its limit with its original acceptance sequence.
* A successfully accepted ID is reserved for the entire engine session, even
  after cancellation or complete fill. Duplicates return `DUPLICATE_ID`.
  Cancelling a missing, filled, or already cancelled order returns `UNKNOWN_ID`.
* Every core ADD/CANCEL attempt consumes one increasing sequence, including
  business rejections. CLI parse failures, BOOK, comments, and QUIT consume none.
  Multiple trades from a command share its sequence and are ordered by their
  position in the result vector/log replay output.
* Validation occurs before matching. No floating-point arithmetic is used.
  Quantities only decrease by a positive value <= their current quantity, so
  subtraction is safe even at the signed 64-bit maximum. The core never sums
  level quantities. Sequence exhaustion throws before state mutation.
* No amend, market order, IOC/FOK, risk limits, fees, or multiple instruments.

## Architecture and tradeoffs

`include/exchange/types.hpp` defines value types. `Engine` owns two ordered maps
of price levels, each with a `std::list<Order>`. An unordered ID map stores
stable list iterators for cancellation, and an unordered set reserves accepted
IDs. `commands.cpp` owns text parsing/presentation; the matching core performs
no I/O. `event_log.cpp` handles persistence/replay. `workload.cpp` generates
commands shared by the generator and benchmark.

New/exhausted levels cost O(log L) in the ordered maps; list operations are O(1).
Hash lookups are expected O(1), with the usual worst-case caveat. Matching costs
O(F + E log L) for F makers visited and E price levels erased. Cancellation finds
the order by hash lookup, then locates its price level in O(log L). Memory is
O(active orders + price levels + all accepted IDs). The lifetime ID set grows
even when the active book stays small. Snapshots allocate a vector; invariant
checks scan the book and are kept out of timed matching.

This favors readable ownership and cancellation correctness over allocation
control or cache locality. Allocations and result-vector construction are part
of the implementation and benchmark. There is one owner thread; callers must
serialize access. Copies of the engine are disabled; moves preserve list
iterators and are explicitly tested. Allocation failure is fatal: the engine
does not promise a strong exception guarantee after memory exhaustion.

## Journal and replay

```sh
./build/exchange_cli --input examples/commands.txt --log session.log
./build/exchange_cli --replay session.log
# Resume an existing session: reconstruct its book and reserved IDs, then append.
./build/exchange_cli --log session.log
```

The binary-opened file is UTF-8/ASCII with literal LF delimiters:

```text
EXCHANGE_LOG_V1
1\tADD 1 SELL 100 10
2\tADD 2 BUY 101 4
```

Here `\t` denotes one actual tab. Every line, including the header and final
record, must end in LF. Records are consecutively numbered canonical ADD/CANCEL
commands. Valid command attempts are logged, including duplicates and unknown
cancellations, so their sequence behavior is reproduced. Parser failures and
queries are excluded. Replay runs the same core and reproduces trade vectors,
remaining orders, sequence, and reserved IDs; the CLI prints the final book.

Each record is appended and flushed **before** applying the command. A write
failure stops the process; do not continue using that session in memory.
Existing logs are validated before appending. Unsupported headers, gaps,
duplicates, noncanonical commands, invalid values, or a final line without LF
cause failure. No file is silently repaired and no partial engine is returned.
The optional C++ replay observer is incremental and may receive a valid prefix
before a later failure; only a successful return establishes a valid full log.

This is an intent journal, not a production durability protocol. Stream flush
does not guarantee storage-device persistence. A crash after append but before
stdout may leave a replayable command whose result the caller never saw.
An allocation failure after journaling likewise requires restarting/replay.
There are no checksums, locking, concurrent writers, transactional acknowledgments,
snapshots, or tamper detection; a syntactically valid edited log will replay.
Only one process may own a journal. Input and journal must be different files.
To recover a truncated journal, preserve the original and explicitly create a
reviewed copy ending at the last complete record; the tool never does this itself.

## Tests and benchmark

CTest runs three entries: core/reference/replay tests, the documented example,
and CLI integration covering errors, stdin, append resume, and truncation.
Core tests cover both crossing directions, maker prices, exact/partial fills,
multiple levels, retained FIFO, middle/front/back cancellation, lifetime IDs,
invalid inputs, maximum numeric values, deterministic workload generation, and
replay equivalence including rejected attempts. Twenty seeds x 2,000 commands
compare every result and snapshot against an independent flat-vector reference
model, check book/index invariants, and check quantity conservation.

```sh
./build/exchange_generate --orders 100000 --seed 42 > workload.txt
./build/exchange_cli --input workload.txt
./build/exchange_bench --orders 100000 --seed 42 --samples 5 --latency
```

`--orders` counts command attempts, including cancellations. The generator uses
standardized `mt19937_64` output with explicit modulo mapping: about 80% new
orders and 20% cancellations of earlier IDs (often already filled). Buy/sell
are equiprobable, prices 9900..10100, quantities 1..100. Modulo mapping has a tiny
bias and is a workload choice, not a statistical-market model. Limit: ten million
commands; repetitions 1..100; seed accepts the unsigned 64-bit range, including 0.

The benchmark pre-generates the entire workload, runs one full untimed warm-up,
then measures fresh engines with `steady_clock`. Timed throughput includes core
ADD/CANCEL, allocations, result destruction, and small result-accounting overhead;
it excludes generation, parsing, formatting, logging, engine construction and
teardown, and invariant checking. Results include rejected cancellations.
Samples reuse one seed; the reported median is the upper middle sample for an
even sample count. A separate optional latency pass timestamps each command,
excluding result accounting/destruction and including clock overhead. Percentiles
use floor((N-1)*p/100) sorted sample indices; no interpolation or overhead subtraction.
An empty clock-pair median helps contextualize instrumented values.

No timing-based test assertions, CPU affinity, cache flushing, real-market traces,
network latency, or production-HFT claim. See [measured results](docs/BENCHMARK.md)
for the actual local run and limitations. TCP/networking is a later milestone.
