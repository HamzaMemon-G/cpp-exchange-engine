# Cancellation experiment and broader measurements

Measured on 2026-10-13 on the laptop, separately from the original PC's
[historical benchmark](BENCHMARK.md). These are synthetic matching-engine
measurements, not market, network or production HFT performance.

## Question and change

The original cancellation path found an order through the hash ID index, then
searched the price map again using the saved side/price. The index already owned
a stable order iterator; retaining its stable price-level iterator removes that
second search. The new tagged handle distinguishes bid/ask levels without
depending on whether their iterator types happen to be identical in a library.
The engine's matching, ID reservation, sequence and journal semantics are unchanged.

The ownership invariant is important: erasing one map node invalidates only that
level's handles. A filled maker's ID entry is erased before the maker/empty level;
a cancelled last order removes its ID entry in the same command. No active entry
can outlive its level. Tests include moves into populated engines, cancelling
orders on both sides after moves, and sweeping then deleting remaining levels.

## Method

Baseline: `0fc53c14f666df895d7298c0ee522e523a95c394`, with the expanded harness
and original engine. Candidate: the iterator change in this PR. Both executables
were built with the same compiler, flags and harness; their SHA-256 hashes are
in [paired-suite-raw.json](paired-suite-raw.json).

* Intel Core i5-8250U CPU @ 1.60 GHz, four cores/eight logical processors.
* Windows 11 Pro, OS version 10.0.26300, x86-64.
* LLVM-MinGW Clang 23.1.3 / libc++, CMake 4.4.4, Ninja 1.13.2.
* Release `-O3 -DNDEBUG`, C++20; no LTO, affinity or priority changes.
* Five profiles, 200,000 timed attempts each, seeds 42/43/44.
* Five paired rounds per profile/seed, alternating baseline/candidate process
  order. Each process runs one complete untimed warm-up and seven timed samples.
  This gives 15 paired process medians per profile, 150 timing processes in total.
* Fresh engines and untimed setup precede every timed sample. Generation, result
  fingerprinting, book comparisons, invariant checks and teardown are untimed.
  The timed loop includes allocations, result destruction and result counters.
* The runner checks the complete untimed result/trade/final-order fingerprint,
  command fingerprint, counters and book metrics across binaries and memory passes.
  All comparisons passed. Reference-model tests remain the independent oracle.
* Background load, power plan, CPU frequency, thermal state and allocator state
  were not controlled. Report paired observations, not confidence bounds.

Deep/cancel-heavy use 4,096 levels per side and eight initial orders per level
(65,536 setup orders). Sweep uses 64 levels and eight makers per level (512 makers
per taker). The sweep and ID-churn streams are intentionally seed-independent;
their three seed labels repeat the same stream and measure run variability.

## Throughput observations

Rates are medians of the 15 process medians, in millions of command attempts/s.
The change column is the median of paired candidate/baseline ratios; it is not
computed by dividing the two independently aggregated rate columns.

| Profile | Baseline M attempts/s | Candidate M attempts/s | Paired median change | Candidate faster pairs |
|---|---:|---:|---:|---:|
| Mixed | 3.162 | 3.180 | -0.3% | 7/15 |
| Deep | 2.046 | 2.123 | +4.0% | 13/15 |
| Cancel-heavy | 2.065 | 2.265 | **+9.7%** | **15/15** |
| Sweep | 3.368 | 3.368 | -1.0% | 7/15 |
| ID churn | 6.904 | 7.014 | +1.4% | 10/15 |

Cancel-heavy paired gains ranged from +6.7% to +21.2%; the median was +9.665%.
The results support a benefit for deep books with frequent successful cancels.
Mixed/sweep and small differences in ID churn are close to desktop run noise;
this is not evidence of a universal speedup. No timing threshold is a CI test.
The older 3.98 M attempts/s figure used a different PC, compiler, count and harness;
it is not the baseline for this experiment.

## Memory observations

Each executable/profile/seed ran one separate fresh `--memory-only` process,
30 processes total. The table gives median process working set in MiB after all
commands and before invariant checking, across three seeds. It includes the
workload vector, runtime, engine, allocations and retained allocator pages.
It is neither isolated engine memory nor allocated-byte accounting.

| Profile | Baseline MiB | Candidate MiB | Candidate active orders, seed 42 | Reserved IDs, seed 42 |
|---|---:|---:|---:|---:|
| Mixed | 22.33 | 22.26 | 27,394 | 159,624 |
| Deep | 49.50 | 49.52 | 184,910 | 225,223 |
| Cancel-heavy | 30.72 | 30.72 | 64,590 | 165,063 |
| Sweep | 19.66 | 19.65 | 443 | 200,000 |
| ID churn | 15.70 | 15.70 | 0 | 100,000 |

There is no material process-memory difference in these observations. Do not
infer a portable `sizeof(Location)` or bytes/order from working-set measurements.
The empty ID-churn book still reserves 100,000 IDs: cancellation frees active
orders, not the session's lifetime ID history. Resetting/restarting an engine is
the current boundary for that history. The report retains every memory checkpoint,
including process lifetime peak and post-destruction observations.

## Profiling evidence

The hosted GCC/gprof workflow captures whole-process call profiles, including
warm-up, setup, generation and verification, so it is not a timed-loop-only
profile. It uses `-pg -fno-inline -fno-ipa-icf -fno-optimize-sibling-calls` in an
instrumented Release build. These flags substantially change execution cost;
the instrumented rates are not compared with normal Release measurements.

The baseline call graph from
[run 38411823536](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38411823536)
records 1,504,152 bid-map `find` calls and 1,501,698 ask-map `find` calls from
`Engine::cancel`: 3,005,850 tree searches. This equals 500,975 successful cancels
per stream times six passes (one warm-up and five samples). It confirms the
redundant operation the change targets. Profiling self-time percentages can be
ambiguous for merged/weak library symbols; the claim here uses these auditable
call counts and the separate uninstrumented paired experiment.

## Reproduction

Build the baseline and candidate in separate directories with identical Release
compiler settings. The baseline must include this harness; the original `main`
binary does not understand the added profiles. Keep the compiler's runtime DLLs
available on Windows. Then run:

```sh
python tools/benchmark_suite.py --baseline /path/to/baseline/exchange_bench \
  --binary /path/to/candidate/exchange_bench --orders 200000 --samples 7 \
  --rounds 5 --seeds 42 43 44 --machine "CPU, OS, compiler and flags" \
  --output work/paired-suite.json
```

For independent memory observations, launch `exchange_bench --memory-only` in a
fresh process for each case. For call profiling, run the CPU profile Actions
workflow and download its `cpu-profiles` artifact. Subsequent measurements should
record their own machine/flags and retain raw results rather than replacing
these dated observations.
