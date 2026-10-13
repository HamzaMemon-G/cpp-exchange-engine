# Order-type extension measurements

These are local synthetic development measurements on 2026-10-13, not exchange
or production HFT performance. Laptop: Intel Core i5-8250U, Windows 11 Pro
10.0.26300, LLVM-MinGW Clang 23.1.3/libc++, CMake Release (`-O3 -DNDEBUG`).
CPU frequency, temperature, scheduling and background activity were not controlled.

## New command workloads

Candidate source: `93dd61d1aedd568a9b24adf2f472597a26593cd3`. The later fuzzing
and documentation additions do not change this matching implementation.
Each workload uses 200,000 timed attempts, seeds 42/43/44, five fresh processes
per seed and seven samples per process. Each process warms up once, then each
sample uses a fresh engine. Setup, generation, snapshots, logging and invariant
checks are outside timing; command dispatch, matching, allocations, result
destruction and accounting are included. This is 30 timing processes and six
additional fresh memory processes, without a throughput warm-up in memory mode.

| Profile | Median of 15 process medians, attempts/s | After-command working set, range across seeds |
|---|---:|---:|
| aggressive | 3,420,762 | 19,320,832–19,386,368 bytes |
| amend-heavy | 1,553,734 | 30,371,840–30,380,032 bytes |

Aggressive mixes roughly 50% GTC, 15% IOC, 15% market, 10% attempted cancel and
10% attempted amendment. Cancels/amendments choose earlier IDs, often no longer
active. At seed 42: 160,199 accepted attempts, 39,801 rejected, 135,317 trades,
1,075,242 units expired, 720 final active orders and 159,905 reserved IDs.
The denominator includes rejected commands, so this is not a fill-only rate.

Amend-heavy preloads 65,536 orders across 4,096 levels per side, depth eight,
outside timing. Its orders remain noncrossing; live-target tracking makes
amendments and cancellations successful. Roughly 75% of attempts amend, 15% add
and 10% cancel. Amendments choose quantity 1..100 and either retain their price or
attempt a new price on the same noncrossing side. This intentionally isolates
live amendment handling from trade generation. At seed 42: all 200,000 attempts
accepted, including 150,040 amendments, 29,965 adds and 19,995 cancels; no trades
or expiry. Final book: 75,506 active orders and 95,501 reserved IDs.

Process working set includes the generated workload, runtime, allocator and
engine, with allocator-retained pages. It is not an engine allocation count or
per-order byte estimate. Each measured sample checks counters and the complete
final book against its warm-up; untimed result/trade/book fingerprints also agree
with the separate memory pass. Independent reference tests cover both profiles.

Raw commands, sample outputs, executable SHA-256, counters and memory checkpoints:
[order-type-suite-raw.json](order-type-suite-raw.json).

```sh
python tools/benchmark_suite.py --binary ./build/exchange_bench \
  --profiles aggressive amend-heavy --orders 200000 --samples 7 \
  --rounds 5 --seeds 42 43 44 --output work/order-type-suite.json
```

## Compatibility and observed cost on historical streams

The pre-extension optimized engine at
`013a3263df2571b6e1733ea856fc31eb0576c731` was rebuilt with the same compiler and
flags and compared with the first feature implementation at
`7af5db013f2f3611761a3d092bf028d83bb54f23` (the following example/docs commit
does not change its executable). Five historical profiles use three seeds,
five alternating baseline/candidate pairs per seed and seven samples per
process: 150 timing processes and 30 separate memory processes.

All workloads, result/trade/final-order fingerprints, counters and final-book
metrics agree across the two implementations. Zero-expiry result fingerprints
retain the historical encoding; nonzero expiry appends a tagged quantity.
These are reproducibility checks, not cryptographic integrity checks.

| Profile | Median paired rate change | Candidate faster / 15 pairs |
|---|---:|---:|
| mixed | +1.11% | 8 |
| deep | -1.73% | 2 |
| cancel-heavy | -1.40% | 5 |
| sweep | -3.56% | 5 |
| id-churn | -4.20% | 4 |

This comparison includes expanded result fields and the new command-accounting
switch in the candidate harness; it does **not** isolate matching-core cost.
Pair ranges are wide under desktop conditions (retained in the raw report).
The feature extension is not a general speedup, and these observations do not
establish a portable regression percentage or statistical significance.
The earlier [cancellation optimization experiment](PERFORMANCE.md) remains a
separate dated comparison of an identical harness before/after its iterator change.

Raw paired observations: [features-compatibility-suite-raw.json](features-compatibility-suite-raw.json).
Do not compare these laptop rates directly with the original PC's 3.98 M/s.
