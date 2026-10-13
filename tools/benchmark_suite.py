#!/usr/bin/env python3
"""Fresh-process memory and interleaved throughput measurements; standard library only."""
import argparse
import hashlib
import json
import platform
import re
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def fields(line):
    result = {}
    for key, value in re.findall(r"(\w+)=([^\s]+)", line):
        try:
            result[key] = float(value) if "." in value else int(value)
        except ValueError:
            result[key] = value
    return result


def run(binary, case, seed, args, memory=False):
    command = [str(binary), "--profile", case["profile"], "--levels", str(case["levels"]),
               "--depth", str(case["depth"]), "--orders", str(args.orders), "--seed", str(seed)]
    command += ["--memory-only"] if memory else ["--samples", str(args.samples)]
    process = subprocess.run(command, capture_output=True, text=True, check=True, timeout=300)
    lines = process.stdout.splitlines()
    metadata = next(fields(line) for line in lines if line.startswith("profile="))
    summary = next(fields(line) for line in lines
                   if line.startswith("memory_result" if memory else "median_commands_per_second="))
    book = next(fields(line) for line in lines
                if line.startswith("memory phase=after_commands" if memory else "final_book"))
    semantic = {key: summary[key] for key in
                ("accepted", "rejected", "adds", "cancels", "trades", "volume", "result_fingerprint")}
    semantic.update({key: book[key] for key in ("active_orders", "bid_levels", "ask_levels", "reserved_ids")})
    semantic.update({key: summary.get(key, 0) for key in ("iocs", "markets", "amendments", "expired")})
    semantic["workload_fingerprint"] = metadata["workload_fingerprint"]
    return {"command": command, "stdout": process.stdout, "stderr": process.stderr,
            "metadata": metadata, "summary": summary, "book": book, "semantic": semantic}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="candidate executable")
    parser.add_argument("--baseline", type=Path, help="optional baseline executable with the same harness")
    parser.add_argument("--orders", type=int, default=200000)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--seeds", type=int, nargs="+", default=[42, 43, 44])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--machine", default="unspecified")
    parser.add_argument("--profiles", nargs="+", choices=["mixed", "deep", "cancel-heavy", "sweep", "id-churn", "aggressive"],
                        default=["mixed", "deep", "cancel-heavy", "sweep", "id-churn"])
    args = parser.parse_args()
    if not 1 <= args.orders <= 10000000 or not 1 <= args.samples <= 100 or not 1 <= args.rounds <= 100:
        parser.error("orders 1..10000000; samples and rounds 1..100")
    if not args.seeds or any(seed < 0 or seed >= 2**64 for seed in args.seeds):
        parser.error("seeds must be unsigned 64-bit integers")
    if len(set(args.seeds)) != len(args.seeds):
        parser.error("seeds must be distinct")
    if len(set(args.profiles)) != len(args.profiles):
        parser.error("profiles must be distinct")
    binaries = {"candidate": args.binary.resolve()}
    if args.baseline:
        binaries = {"baseline": args.baseline.resolve(), **binaries}
    cases = [{"name": "mixed", "profile": "mixed", "levels": 1024, "depth": 8},
             {"name": "deep", "profile": "deep", "levels": 4096, "depth": 8},
             {"name": "cancel-heavy", "profile": "cancel-heavy", "levels": 4096, "depth": 8},
             {"name": "sweep", "profile": "sweep", "levels": 64, "depth": 8},
             {"name": "id-churn", "profile": "id-churn", "levels": 1, "depth": 1},
             {"name": "aggressive", "profile": "aggressive", "levels": 1, "depth": 1}]
    cases = [case for case in cases if case["profile"] in args.profiles]
    report = {"created_utc": datetime.now(timezone.utc).isoformat(), "platform": platform.platform(),
              "machine": args.machine, "python": platform.python_version(),
              "method": ("alternating AB/BA fresh processes" if args.baseline else "fresh candidate processes") +
                        "; each process warms up once; harness uses upper-middle median; suite uses statistics.median",
              "orders": args.orders, "samples": args.samples, "rounds": args.rounds, "seeds": args.seeds, "profiles": args.profiles,
              "binaries": {label: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                           for label, path in binaries.items()}, "runs": [], "memory_runs": [], "summary": []}
    expected = {}
    for case in cases:
        ratios, rates = [], {label: [] for label in binaries}
        for seed in args.seeds:
            for round_number in range(args.rounds):
                labels = list(binaries)
                if (round_number + args.seeds.index(seed)) % 2:
                    labels.reverse()
                pair = {}
                for label in labels:
                    result = run(binaries[label], case, seed, args)
                    key = (case["name"], seed)
                    if key in expected and result["semantic"] != expected[key]:
                        raise RuntimeError(f"Semantic mismatch for {key}, {label}")
                    expected[key] = result["semantic"]
                    result.update(label=label, case=case["name"], seed=seed, round=round_number + 1)
                    report["runs"].append(result)
                    pair[label] = result["summary"]["median_commands_per_second"]
                    rates[label].append(pair[label])
                if "baseline" in pair:
                    ratios.append(pair["candidate"] / pair["baseline"])
                print(f"{case['name']} seed={seed} round={round_number + 1}: {pair}", flush=True)
            for label, binary in binaries.items():
                result = run(binary, case, seed, args, memory=True)
                if result["semantic"] != expected[(case["name"], seed)]:
                    raise RuntimeError(f"Memory pass semantic mismatch for {case['name']}, {label}")
                result.update(label=label, case=case["name"], seed=seed)
                report["memory_runs"].append(result)
        summary = {"case": case["name"], "median_process_rates":
                   {label: statistics.median(values) for label, values in rates.items()}}
        if ratios:
            summary.update(median_paired_ratio=statistics.median(ratios),
                           min_paired_ratio=min(ratios), max_paired_ratio=max(ratios),
                           candidate_faster_pairs=sum(ratio > 1 for ratio in ratios), pairs=len(ratios))
        report["summary"].append(summary)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))


if __name__ == "__main__":
    main()
