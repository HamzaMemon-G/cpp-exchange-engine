# Verification record

Local verification on 2026-10-13, Windows with MinGW GCC 14.2.0:

* Debug and Release built using CMake/Ninja with `-Wall -Wextra -Wpedantic
  -Wconversion -Wshadow`; final builds emitted no compiler warnings.
* All three CTest entries passed in Debug and Release: core/reference/replay,
  example command file, and CLI integration.
* Core checks: both crossing directions, resting prices, exact/partial fills,
  multiple price levels, FIFO retention after partial fill, cancellations,
  duplicate/missing IDs, parser rejection and maximum integer values.
* 40,000 randomized commands over 20 fixed seeds: every result and book compared
  with an independent O(n) flat-vector reference model. Book/index/FIFO/noncrossing
  invariants and quantity conservation checked after each command.
* A 3,001-command journal round trip compared all results, trades, book and
  sequences. Includes business rejections and resumed appending. Engine moves and
  resulting index iterators were checked.
* Invalid/truncated journals were rejected without changing their bytes, including
  unsupported headers, missing LF, sequence gaps/repetition, invalid commands,
  invalid values and noncanonical records.
* CLI integration: documented sample output, continuation after parser/business
  errors, exit codes, file/stdin input, replay, append resume, journal truncation,
  and rejecting input/journal aliases.
* A generated 10,000-command workload ran through the CLI with and without logging;
  replay's final book matched the direct run byte for byte.
* Release benchmark: five 100,000-command throughput samples, a separate
  instrumented latency pass, and separate three-sample CLI/journal timing runs.
  See BENCHMARK.md and raw artifacts; no performance pass/fail threshold.
* CI YAML parsed locally and its expected Linux GCC/Clang matrix and Windows job
  were checked. Hosted results were subsequently inspected as recorded below.

All three CTest entries also passed in a separate Debug build with
`-D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS`, checking standard-library iterator usage
and assertions. This includes move construction/assignment and cancellation
through the ID index. Checked-iterator testing is distinct from sanitizer testing.

## Hosted verification

On 2026-10-13, the push run for commit
`e30a5b6b44aaa985094a1c0f2d6803bd2abad14b` completed successfully:
[Actions run 38406561044](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38406561044).
The job logs confirm all three original CTest entries passed under:

* Ubuntu 24.04, GCC 13.3.0, Debug with address and undefined-behavior sanitizers.
* Ubuntu 24.04, Clang 18.1.3, Debug with the same sanitizers.
* Windows hosted runner, MSVC 19.51.36260.0, Release.

These results apply to that commit; later commits need their own successful runs.

## Expanded harness and optimized cancellation

On the laptop, LLVM-MinGW Clang 23.1.3 with CMake 4.4.4 / Ninja 1.13.2 built
Debug and Release without warnings. All four CTest entries passed in each:
core/reference/replay, example CLI, CLI integration, and benchmark profiles.
The original 40,000-command differential test still runs, and five additional
1,000-command workload streams (plus setup) are checked against the reference.
Tests also move multilevel engines into populated destinations and cancel/fill
their remaining orders, exercising both price-level and order handles.

The optimized source commit
`fb876bbc497a559222adeaca9f58c21287cf6eb1` passed hosted PR checks:
[Actions run 38413091696](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38413091696).
Job logs confirm all four CTest entries passed in every configuration:

* Ubuntu 24.04 / GCC 13.3.0 Debug, address and undefined-behavior sanitizers.
* Ubuntu 24.04 / Clang 18.1.3 Debug, the same sanitizers.
* Ubuntu 24.04 / GCC 13.3.0 Debug, `_GLIBCXX_DEBUG` and `_GLIBCXX_ASSERTIONS`.
* Windows hosted runner / MSVC 19.51.36260.0 Release.

The [CPU profile run](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38413079560)
also succeeded. The local paired suite completed 150 timing processes and 30
fresh memory processes with matching command/result fingerprints, counters and
book metrics across the baseline and candidate. See [PERFORMANCE.md](PERFORMANCE.md)
and its retained raw outputs for the experiment and measurement limitations.

## Market, IOC, amendments and parser fuzzing

The new source at `93dd61d1aedd568a9b24adf2f472597a26593cd3` passed
[hosted run 38419796577](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38419796577).
All four CTest entries passed in GCC 13.3 / Clang 18.1.3 Debug with ASan/UBSan,
GCC checked iterators, and MSVC 19.51 Release. The inspected job logs contain
no C++ compiler warnings. Local LLVM-MinGW Clang 23.1.3 Debug and Release also
pass all four entries.

New coverage includes:

* Immediate limit/market matching on both sides, maker prices, FIFO, maximum
  tick/quantity boundaries, explicit expiry and lifetime ID reservation.
* Remaining-quantity amendments: retained priority for same-price reductions
  and no-ops; new priority for increases/price changes; crossing and full fills;
  invalid/unknown IDs and level handles after engine moves.
* 160,000 total randomized command attempts: 40,000 historical GTC/cancel,
  60,000 with immediate orders, and 60,000 including amendments. Every result
  (including expired quantity) and book matches the independent flat-vector
  model, with invariants and quantity conservation checked. The amendment
  stream deliberately targets live orders as well as old/missing IDs.
* All 65,536 four-command streams from a 16-command alphabet, comparing every
  intermediate result and book: 262,144 additional command executions.
* Seven reproducible benchmark profiles checked against the reference, plus
  CLI argument, latency and fresh-memory smoke checks. Timing is never a test
  pass/fail threshold.
* V1 replay/ADD-CANCEL append compatibility; V1 refusal of new commands without
  writing; V2 replay/resume including expiry and amendments; validated-copy
  upgrades, unchanged sources, overwrite refusal and reserved IDs across upgrade.
* The new documented command/output example is checked byte for byte by CLI tests.

The first optional Clang 18.1.3/Linux fuzz pass at
`6fe539ed5107c38bc530b659091ec9be0179ac7f` succeeded:
[run 38420495243](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38420495243).
Its log reports **574,526 mutated text inputs in 121 seconds**, with no sanitizer
finding or failed property check. Inputs are bounded to 4,096 bytes / 64 commands;
this count is not a count of accepted trading commands. See [FUZZING.md](FUZZING.md)
for reproduction and limits. The [artifact](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38420495243/artifacts/11826367571)
contains its full fuzzer log.

The observed Node 20 action warnings prompted verified release updates to
[checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1) and
[upload-artifact v7.0.2](https://github.com/actions/upload-artifact/releases/tag/v7.0.2),
pinned by exact commits. Workflows use read-only repository permissions and do
not retain checkout credentials. The updated source/workflows at
`85637382544b690746627d56008549326cf5f212` passed
[all four CTest configurations](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38421149699)
and the [stronger fuzz pass](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38421149668):
573,355 mutated inputs in 121 seconds, with no findings. Its
[retained raw output](fuzz-run-8563738.txt) includes the exact command and source.
The updated [seven-profile workflow](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38421141254)
also passed and uploaded its CPU profile artifact. Instrumented gprof rates are
not Release performance measurements. Later commits need their own successful checks.

The feature measurements completed 150 historical-stream timing processes plus
30 memory processes with identical baseline/candidate semantic fingerprints,
then 30 timing and six memory processes for aggressive/live-amendment workloads.
[ORDER_TYPE_MEASUREMENTS.md](ORDER_TYPE_MEASUREMENTS.md) retains observations and
limitations, including the small observed slowdowns and changed accounting cost.

## Verification limits

Linux, Clang, and MSVC were not run locally on the original PC. No
address/undefined-behavior sanitizer runtime was present in its MinGW toolchain;
sanitizers were verified on hosted Linux as recorded above. No crash/power-loss
fault injection, device persistence, simultaneous writers, memory-exhaustion
recovery, or authenticated/real-market integration was tested.

The sequence-exhaustion guard is implemented but not exercised by iterating to
2^64-1 commands. Extremely large books and adversarial hash behavior are outside
these measurements. Replay checks structure and canonical format; it cannot detect
a syntactically valid edited event. See README for the fatal-allocation contract
and journal durability limitations.
