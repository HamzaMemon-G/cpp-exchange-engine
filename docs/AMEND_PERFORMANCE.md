# Same-price amendment experiment

An increase of remaining quantity at an unchanged price loses FIFO priority.
The first amendment implementation removed its list node and ID-index entry,
attempted matching, then allocated/reindexed a replacement. The unchanged price
cannot cross the opposite book: changing quantity does not change the existing
noncrossing price invariant. Rebuilding that order is unnecessary.

The candidate assigns the amendment sequence, sets the new remaining quantity
and uses single-element `list::splice` to move the existing node to its queue's
back. The stored level/order handles and ID entry remain valid. Reductions and
no-ops still retain priority; price changes still remove, match and rest a new
remainder. The [C++ list operation contract](https://eel.is/c++draft/list.ops#7)
preserves element iterators, allows this same-list operation (including an order
already at the back), throws nothing and takes constant time. The fast path
requires one expected-O(1) ID lookup and performs no node reconstruction.

## Paired Release measurements

Baseline: `d77bc7e0e7f17b389666affbe9e2b2d374a30ea1`.
Candidate: `b3abe16efe92f01879199951b008829ffaca6e77`.
Both use the identical harness and compiler/flags: laptop Intel Core i5-8250U,
Windows 11 Pro 10.0.26300, LLVM-MinGW Clang 23.1.3/libc++, Release `-O3 -DNDEBUG`.
Unlike the feature-extension comparison, this isolates a matching implementation
change with the same accounting code and result layout.

Two profiles, 200,000 attempts, seeds 42/43/44, five alternating baseline/candidate
process pairs per seed, seven samples per process and one full untimed warm-up.
Every sample uses a fresh engine and untimed setup. In total: 60 timing processes
and 12 fresh memory processes. Workload/result/trade/final-book fingerprints,
counters and metrics agree across binaries and memory passes.

| Profile | Baseline median process rate | Candidate median process rate | Median paired change | Candidate faster / 15 |
|---|---:|---:|---:|---:|
| amend-heavy | 1,666,713/s | 2,024,179/s | **+21.47%** | 15 |
| aggressive | 3,847,042/s | 3,838,808/s | -0.61% | 6 |

Amend-heavy preloads 65,536 noncrossing orders across 4,096 levels per side,
depth eight, and targets live orders: about 75% amendments, 15% adds, 10% cancels.
Its paired ratios range from 1.1933 to 1.3694. Aggressive mostly chooses old IDs
for amendment/cancel attempts, so the new path is uncommon there; its paired
ratios range from 0.8990 to 1.0662. This is a targeted synthetic improvement,
not a universal throughput gain. Desktop scheduling, temperature, frequency and
background activity are uncontrolled; no affinity, cache flushing or LTO is used.
Memory is process-wide working set, not an isolated engine allocation measurement.

[amend-splice-suite-raw.json](amend-splice-suite-raw.json) retains all commands,
sample outputs, executable hashes, counters and memory checkpoints. Reproduce:

```sh
python tools/benchmark_suite.py --binary ./build/exchange_bench \
  --baseline /path/to/d77bc7e/exchange_bench --profiles aggressive amend-heavy \
  --orders 200000 --samples 7 --rounds 5 --seeds 42 43 44 \
  --output work/amend-splice-suite.json
```

A shorter historical-stream follow-up used the five original profiles, three
seeds, one pair per seed and three samples. All semantic fingerprints/counters
and books still match, including fresh memory passes. The small sample and wide
desktop variation do not establish general performance equivalence. Its raw
observations are retained in [amend-compatibility-raw.json](amend-compatibility-raw.json).

## Call-count evidence

Hosted GCC 13.3 gprof runs use `-pg -fno-inline -fno-ipa-icf
-fno-optimize-sibling-calls`, one million amend-heavy attempts, seed 42, 1,024
levels per side/depth eight, five samples plus warm-up. Setup is applied six
times. These are whole-process instrumented profiles; their rates and sampled
self-time percentages are not used as Release performance measurements.

Before: [run 38421141254](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38421141254),
source `8563738` (the following docs-only commit is the Release baseline).
After: [run 38422408315](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38422408315),
source `b3abe16`. Both have the same workload and result fingerprints.
Both record 4,501,458 amendment calls (750,243 per pass, six passes).

The direct `Engine::amend` → `Engine::rest` call arcs total **3,366,030 before**
and **2,249,886 after**. The difference, **1,116,144**, equals the candidate's
single-element splice entrypoint call count. This demonstrates the avoided
rest/reindex path without relying on ambiguous gprof flat self-time attribution
or claiming an OS-level allocation count. Retained full graphs and benchmark text:
[before graph](profiles/amend-before-gprof.txt),
[after graph](profiles/amend-after-gprof.txt),
[before workload](profiles/amend-before-benchmark.txt),
[after workload](profiles/amend-after-benchmark.txt).

## Correctness verification

Local Debug/Release CTest passed. The optimized source passed
[GCC/Clang ASan+UBSan, GCC checked iterators and MSVC Release](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38422562701),
including the independent model's 160,000 randomized commands and all 65,536
bounded four-command streams. The [Clang parser/state fuzz pass](https://github.com/HamzaMemon-G/cpp-exchange-engine/actions/runs/38422562611)
also passed. In particular, these checks exercise unchanged FIFO semantics,
both-side increases, singleton/already-last orders, partial fills, cancellations,
engine moves and stored iterator validity. No CLI or journal semantics change.
