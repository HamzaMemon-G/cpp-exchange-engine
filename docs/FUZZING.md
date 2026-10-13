# Command parser and state fuzzing

The default build needs no fuzzing packages or runtime. An optional Clang/Linux
target compiles the actual parser and matching core with libFuzzer coverage,
AddressSanitizer and UndefinedBehaviorSanitizer. libFuzzer is provided by the
compiler toolchain; CMake does not fetch dependencies. GCC, MSVC and non-Linux
fuzzer configurations fail with a clear configure error.

```sh
cmake -S . -B build-fuzz -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ -DEXCHANGE_FUZZER=ON
cmake --build build-fuzz --target exchange_command_fuzz --parallel 2
mkdir -p work/fuzz-corpus work/fuzz-artifacts
cp tests/fuzz/corpus/* work/fuzz-corpus/
./build-fuzz/exchange_command_fuzz work/fuzz-corpus \
  -dict=tests/fuzz/commands.dict -max_total_time=120 -timeout=5 \
  -rss_limit_mb=1024 -max_len=4096 -seed=42 -artifact_prefix=work/fuzz-artifacts/
```

The tracked seeds cover FIFO amendments, immediate expiry, duplicate IDs, both
sides and numeric boundaries. The dictionary encourages command words, integer
limits and delimiters. libFuzzer mutates the text and expands its corpus; copy
the seeds to a work directory before running locally to keep the source clean.

Each input is at most 4,096 bytes and executes at most 64 lines on a fresh engine.
Malformed lines are rejected; valid state commands must round-trip through the
canonical formatter. Checks include book/index/FIFO/noncrossing invariants,
sequence advancement, unchanged books on business rejection, positive/distinct
trade participants, price bounds, bounded quantity subtraction and exact immediate
quantity disposition. Accepted IOC/market IDs must not appear in the resting book.
AMEND trade bounds use the existing order's side, which is not part of its text command.

The [fuzz workflow](../.github/workflows/fuzz.yml) runs a bounded two-minute pass
on relevant pushes/PRs and supports manual runs. Its artifact retains the fuzzer
log and any crash reproducer. Build failure, sanitizer findings, failed checks,
timeout or excessive memory fail the job. Reproduce a saved input with
`exchange_command_fuzz path/to/crash-file` under the same compiler/runtime.

Fuzz execution totals count mutated text inputs, **not** accepted trading commands.
This complements the independent reference-model tests and exhaustive short
sequences; it does not compare every fuzzed command with that reference. A passing
bounded run is not proof of correctness. It does not fuzz journal migration,
large books, allocation failure, concurrent access or device durability.
