#!/usr/bin/env python3
import csv
import os
import sys

import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PATH = os.path.join(ROOT, "results.csv")
OUT_DIR = os.path.join(ROOT, "output")


def load_rows():
    with open(CSV_PATH, newline="", encoding="utf-8") as f:
        r = csv.DictReader(f)
        return list(r)


def main():
    if not os.path.isfile(CSV_PATH):
        print("Missing", CSV_PATH, file=sys.stderr)
        sys.exit(1)
    os.makedirs(OUT_DIR, exist_ok=True)
    rows = load_rows()
    for row in rows:
        row["threads"] = int(row["threads"])
        row["ops_per_sec"] = float(row["ops_per_sec"])
        row["p99_ns"] = float(row["p99_ns"])
        row["mem_delta_ws"] = int(row["mem_delta_ws"])
        row["verify_errors"] = int(row["verify_errors"])

    profiles = sorted({r["profile"] for r in rows})

    for prof in profiles:
        sub = [r for r in rows if r["profile"] == prof]
        impls = sorted({r["impl"] for r in sub})
        plt.figure(figsize=(9, 5))
        for impl in impls:
            pts = sorted([r for r in sub if r["impl"] == impl], key=lambda x: x["threads"])
            xs = [p["threads"] for p in pts]
            ys = [p["ops_per_sec"] for p in pts]
            plt.plot(xs, ys, marker="o", label=impl)
        plt.xlabel("Threads")
        plt.ylabel("Operations / s")
        plt.title(f"Throughput ({prof})")
        plt.legend()
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        p = os.path.join(OUT_DIR, f"throughput_{prof}.png")
        plt.savefig(p, dpi=150)
        plt.close()
        print(p)

    for prof in profiles:
        sub = [r for r in rows if r["profile"] == prof]
        impls = sorted({r["impl"] for r in sub})
        plt.figure(figsize=(9, 5))
        for impl in impls:
            pts = sorted([r for r in sub if r["impl"] == impl], key=lambda x: x["threads"])
            xs = [p["threads"] for p in pts]
            ys = [p["p99_ns"] / 1000.0 for p in pts]
            plt.plot(xs, ys, marker="o", label=impl)
        plt.xlabel("Threads")
        plt.ylabel("p99 latency, us")
        plt.title(f"p99 latency ({prof})")
        plt.legend()
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        p = os.path.join(OUT_DIR, f"latency_p99_{prof}.png")
        plt.savefig(p, dpi=150)
        plt.close()
        print(p)

    sub = [r for r in rows if r["profile"] == "mixed" and r["threads"] == 64]
    if sub:
        impls = sorted({r["impl"] for r in sub}, key=lambda x: x)
        vals = []
        for impl in impls:
            v = next((int(r["mem_delta_ws"]) for r in sub if r["impl"] == impl), 0)
            vals.append(v / (1024.0 * 1024.0))
        plt.figure(figsize=(8, 4))
        plt.bar(impls, vals)
        plt.ylabel("Delta working set, MiB")
        plt.title("Working set delta (mixed, 64 threads)")
        plt.xticks(rotation=20)
        plt.grid(True, axis="y", alpha=0.3)
        plt.tight_layout()
        p = os.path.join(OUT_DIR, "memory_delta_mixed_64.png")
        plt.savefig(p, dpi=150)
        plt.close()
        print(p)

    err_by_impl = {}
    for r in rows:
        err_by_impl[r["impl"]] = err_by_impl.get(r["impl"], 0) + int(r["verify_errors"])
    if err_by_impl:
        impls = sorted(err_by_impl.keys())
        vals = [err_by_impl[i] for i in impls]
        p = os.path.join(OUT_DIR, "reliability_verify_errors.png")
        if all(v == 0 for v in vals):
            if os.path.isfile(p):
                try:
                    os.remove(p)
                except OSError:
                    pass
        else:
            plt.figure(figsize=(8, 4))
            plt.bar(impls, vals)
            plt.ylabel("Total verify errors")
            plt.title("Read consistency check failures")
            plt.xticks(rotation=20)
            plt.grid(True, axis="y", alpha=0.3)
            plt.tight_layout()
            plt.savefig(p, dpi=150)
            plt.close()
            print(p)


if __name__ == "__main__":
    main()
