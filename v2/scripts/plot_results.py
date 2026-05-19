#!/usr/bin/env python3
"""Plot CAIDJ benchmark JSON output.

Usage:
    python scripts/plot_results.py results/results.json
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: plot_results.py <results.json>", file=sys.stderr)
        return 2

    try:
        import matplotlib.pyplot as plt
        import pandas as pd
    except ImportError as exc:
        print(f"Missing optional plotting dependency: {exc}", file=sys.stderr)
        print("Install with: pip install matplotlib pandas", file=sys.stderr)
        return 1

    path = Path(sys.argv[1])
    payload = json.loads(path.read_text(encoding="utf-8"))
    rows = payload.get("results", [])
    if not rows:
        print("No results found in JSON file", file=sys.stderr)
        return 1

    df = pd.DataFrame(rows)
    out_dir = path.parent

    plots = [
        ("join_latency_ms", "Latency (ms)", "latency_vs_concurrency.png"),
        ("throughput_jps", "Throughput (joins/s)", "throughput_vs_concurrency.png"),
        ("lock_contention_rate", "Contention rate", "contention_vs_concurrency.png"),
        ("memory_overhead_mb", "Memory overhead (MB)", "memory_overhead.png"),
    ]

    for column, ylabel, filename in plots:
        plt.figure(figsize=(9, 5))
        for protocol, group in df.groupby("protocol"):
            grouped = group.groupby("concurrency", as_index=False)[column].mean()
            plt.plot(grouped["concurrency"], grouped[column], marker="o", label=protocol)
        plt.xlabel("Writer concurrency")
        plt.ylabel(ylabel)
        plt.title(f"CAIDJ {ylabel} vs Concurrency")
        plt.grid(True, alpha=0.3)
        plt.legend()
        plt.tight_layout()
        plt.savefig(out_dir / filename, dpi=150)
        plt.close()

    print(f"Plots written to {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())