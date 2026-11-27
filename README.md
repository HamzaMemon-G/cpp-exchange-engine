# C++ Exchange Matching Engine

[![C++ checks](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/workflows/ci.yml)
[![Command fuzzing](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/workflows/fuzz.yml/badge.svg?branch=main)](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/workflows/fuzz.yml)

A standalone C++20 simulation of a single-instrument exchange matching engine. Submit limit, immediate-or-cancel, and market orders; amend or cancel them; inspect the book; and replay a journal. The matching core is single-threaded and CPU-only. This is a command-line project with no GUI, exchange connection, real-money trading, account data, or network service.

## Try it

With CMake and a C++20 compiler installed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/exchange_cli --input examples/order-types.txt
```

Windows PowerShell with MinGW and Ninja on `PATH`:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
.\build\exchange_cli.exe --input examples\order-types.txt
```

The CLI prints accepted orders, trades, expirations, and book snapshots. Run it without `--input` to type commands interactively, or use `--help` to see options. CMake 3.20 or newer is required. See the [complete example input](examples/order-types.txt) and its [expected output](examples/order-types-output.txt).

For example, the sample rests two buy orders at 100. A market sell first fills the oldest order; a later market sell sweeps the next order:

```text
TRADE sequence=4 maker=1 taker=3 price=100 quantity=2
BOOK sequence=5
BUY price=100 remaining=8 id=2 sequence=2
BUY price=100 remaining=7 id=1 sequence=5
END_BOOK
```

## What it demonstrates

- Best-price matching, FIFO priority at each price, and trades at the resting order's price.
- Partial fills and orders sweeping multiple price levels.
- GTC limit, IOC limit, and market orders, with immediate leftovers explicitly expired.
- Amendments with defined FIFO behavior: same-price reductions keep priority; increases move to the back; price changes re-enter matching.
- Cancellation by ID and lifetime reservation of accepted IDs.
- A versioned append-only command journal, deterministic replay, resume, and validated V1-to-V2 log-copy upgrade.
- A scriptable CLI, deterministic workload generator, benchmark harness, independent reference model, and optional sanitizer-backed command fuzzer.

## Design

The engine stores bid and ask price levels in ordered maps. Each level contains a FIFO list of orders; an ID index retains stable handles for cancellation and amendment. This supports best-price lookup, constant-time queue operations, and expected constant-time ID lookup while keeping matching ownership in one thread.

The journal records accepted command attempts before applying them and can rebuild the book and reserved IDs by replay. It is an educational intent log, not a production durability protocol: flush does not guarantee device persistence, and the format has no checksums, concurrent-writer coordination, or tamper detection.

## Validation and performance

The main branch passes hosted Linux GCC and Clang sanitizer builds, checked-iterator tests, Windows MSVC tests, and bounded command fuzzing. Local Debug and Release runs each pass all four CTest entries. Tests compare randomized command streams and bounded exhaustive streams against an independent reference implementation. See [validation details](docs/VALIDATION.md).

Benchmarks use deterministic synthetic workloads, warm-ups, fresh engines, and separate matching-core and process-memory measurements. They are reproducible engineering experiments, not production or HFT performance claims. A same-price amendment fast path measured a 21.47% median paired improvement on its targeted amend-heavy workload and was slightly slower on the aggressive workload. The raw data, setup, and limitations are documented in [the amendment report](docs/AMEND_PERFORMANCE.md); see also [broader performance measurements](docs/PERFORMANCE.md) and [order-type measurements](docs/ORDER_TYPE_MEASUREMENTS.md).

## Project guide

- [Order types, FIFO rules, and journal migration](docs/ORDER_TYPES.md)
- [Architecture, workloads, and benchmark method](docs/PERFORMANCE.md)
- [Original benchmark and measurement limits](docs/BENCHMARK.md)
- [Fuzzing setup and workflow](docs/FUZZING.md)
- [Validation record and hosted runs](docs/VALIDATION.md)

## Scope

This is a learning and portfolio project. It does not implement a network protocol, multiple instruments, risk checks, fees, FOK orders, persistence guarantees, or production exchange behavior. The benchmark does not use real market data and should not be compared directly with production systems.

No license file is included yet. Contact the author before reusing or redistributing the code.
