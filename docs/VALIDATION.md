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
