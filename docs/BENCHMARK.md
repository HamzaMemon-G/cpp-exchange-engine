# Local measurements

Measured 2026-10-13 on this PC. These are synthetic development measurements,
not exchange, network, or production HFT performance claims.

Environment:

* Intel Core i5-9400F CPU @ 2.90 GHz; six logical processors reported.
* 64-bit Windows, OS API description `Microsoft Windows 10.0.26300`.
* GCC 14.2.0, MinGW-W64 x86_64-ucrt-posix-seh; CMake 3.30.4; Ninja 1.12.1.
* CMake Release (`-O3 -DNDEBUG`), C++20, no LTO, affinity, or priority changes.
* One matching thread; regular desktop conditions. Background load, CPU frequency,
  power plan, and thermal state were not controlled. Memory usage was not measured.

## Matching-core benchmark

```text
exchange_bench --orders 100000 --seed 42 --samples 5 --latency
```

Workload generation is outside timing. One complete untimed warm-up precedes
five runs with fresh engines, all processing the same command stream. About 80%
are adds and 20% attempted cancellations; prices are 9900..10100 and quantities
1..100. Throughput includes command execution, allocations, result destruction,
and small counters; it excludes text parsing, snapshots, journal and stdout I/O,
engine construction/destruction, and post-run invariant checking.

| Sample | Command attempts / second |
|---|---:|
| 1 | 4,009,864 |
| 2 | 3,969,798 |
| 3 | 3,975,590 |
| 4 | 4,040,045 |
| 5 | 3,891,247 |
| Median | **3,975,590** |

Each sample: 83,239 accepted commands, 16,761 rejections, 61,972 trades,
1,581,739 units of executed quantity. Rejections are mostly cancellations of
orders already filled or cancelled. They count in the throughput denominator.
This high-rejection, bounded-price-range workload is not representative of all
books; results should not be extrapolated to other order/cancel distributions.

The separate instrumented latency pass reported p50 300 ns, p95 500 ns,
p99 600 ns, and maximum 560,700 ns. These include per-operation timing overhead,
exclude result destruction/accounting, and use the sorted-index percentile rule
in the README. The empty clock-pair median was **0 ns**, demonstrating limited
timer granularity for these short measurements. Treat the percentiles as rough,
instrumented observations; they do not establish accurate sub-microsecond
service latency. The maximum is sensitive to scheduling, allocations, and desktop
activity. No baseline subtraction was applied.

Raw harness output: [benchmark-raw.txt](benchmark-raw.txt).

## CLI and journal I/O, measured separately

A 10,000-command seed-42 file, followed by BOOK, was generated with
`exchange_generate`. Python `perf_counter` measured the complete CLI subprocess.
Each mode had one full untimed warm-up followed by three timed fresh sessions;
stdout was redirected to the null device. This includes process launch, parsing,
formatting, output writes, and engine teardown. The journal mode used a new file
for every run and flushed every command before matching; no device-level sync.

| Mode | Median wall time | Attempts / second |
|---|---:|---:|
| CLI, no journal | 153.786 ms | 65,025 |
| CLI, append/flush journal | 243.022 ms | 41,148 |

These rates are end-to-end subprocess measurements and cannot be substituted
for matching-core throughput. Process startup is substantial for a short run;
disk caching, antivirus, and filesystem behavior were not controlled. The final
BOOK includes snapshot formatting. No standalone disk-only latency was measured.
The generated workload's final book matched replay byte for byte.

Raw timing samples and workload SHA-256: [io-raw.json](io-raw.json).

## Reproduction

Build Release and run the command above. The same workload and counters should
recur for seed 42; timing will vary. To reproduce the end-to-end method, generate
10,000 commands, run the CLI with stdout redirected to a null device, warm up once
per mode, then time three new processes, using a fresh journal path on each run.
Exit code 2 is expected because the workload deliberately includes unknown-ID
cancellations. Keep desktop conditions and file placement in the reported context.
