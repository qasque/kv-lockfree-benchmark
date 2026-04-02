#!/usr/bin/env python3
import csv
import glob
import os
import sys

import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_GLOB = os.path.join(ROOT, "output", "snapshots_long_*.csv")
OUT_DIR = os.path.join(ROOT, "output")


def load_snapshots(path):
    with open(path, newline="", encoding="utf-8") as f:
        r = csv.DictReader(f)
        rows = list(r)
    for row in rows:
        row["t_elapsed_sec"] = float(row["t_elapsed_sec"])
        row["ops_per_sec_window"] = float(row["ops_per_sec_window"])
        row["mem_ws_bytes"] = int(row["mem_ws_bytes"])
        row["verify_errors"] = int(row["verify_errors"])
        row["threads"] = int(row["threads"])
        row["keyspace"] = int(row["keyspace"])
    return rows


def plot_file(path):
    rows = load_snapshots(path)
    if not rows:
        return
    base = os.path.splitext(os.path.basename(path))[0]
    impl = rows[0].get("impl", base)
    prof = rows[0].get("profile", "")
    thr = rows[0].get("threads", "")

    xs = [r["t_elapsed_sec"] for r in rows]
    ys_tp = [r["ops_per_sec_window"] for r in rows]
    ys_mem = [r["mem_ws_bytes"] / (1024.0 * 1024.0) for r in rows]

    plt.figure(figsize=(9, 5))
    plt.plot(xs, ys_tp, marker=".", markersize=3)
    plt.xlabel("Time, s")
    plt.ylabel("Operations / s (window)")
    plt.title(f"Throughput over time: {impl}, {prof}, {thr} threads")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    p1 = os.path.join(OUT_DIR, f"{base}_throughput_time.png")
    plt.savefig(p1, dpi=150)
    plt.close()
    print(p1)

    plt.figure(figsize=(9, 5))
    plt.plot(xs, ys_mem, marker=".", markersize=3, color="C1")
    plt.xlabel("Time, s")
    plt.ylabel("Working set, MiB")
    plt.title(f"Memory over time: {impl}, {prof}, {thr} threads")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    p2 = os.path.join(OUT_DIR, f"{base}_memory_time.png")
    plt.savefig(p2, dpi=150)
    plt.close()
    print(p2)


def plot_compare(paths):
    plt.figure(figsize=(10, 5))
    for path in paths:
        rows = load_snapshots(path)
        if not rows:
            continue
        label = rows[0]["impl"]
        xs = [r["t_elapsed_sec"] for r in rows]
        ys = [r["ops_per_sec_window"] for r in rows]
        plt.plot(xs, ys, marker=".", markersize=2, label=label, alpha=0.85)
    plt.xlabel("Time, s")
    plt.ylabel("Operations / s (window)")
    plt.title("Throughput over time (comparison)")
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    p = os.path.join(OUT_DIR, "snapshots_compare_throughput.png")
    plt.savefig(p, dpi=150)
    plt.close()
    print(p)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    args = sys.argv[1:]
    if args:
        paths = []
        for a in args:
            paths.extend(glob.glob(a) if ("*" in a or "?" in a) else ([a] if os.path.isfile(a) else []))
    else:
        paths = sorted(glob.glob(DEFAULT_GLOB))

    paths = [p for p in paths if os.path.isfile(p)]
    if not paths:
        print("No snapshot CSV. Add output/snapshots_long_*.csv or pass paths.", file=sys.stderr)
        sys.exit(1)

    for path in paths:
        plot_file(path)
    if len(paths) >= 2:
        plot_compare(paths)


if __name__ == "__main__":
    main()
