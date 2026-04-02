#!/usr/bin/env python3
import csv
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BENCH = os.path.join(ROOT, "bench.exe")
OUT_DIR = os.path.join(ROOT, "output")

IMPLS = ["mutex", "rwlock", "skiplock", "lfhash", "lfskip"]

SECONDS = int(os.environ.get("LONG_SECONDS", "300"))
SNAPSHOT_INTERVAL_SEC = int(os.environ.get("LONG_INTERVAL", "15"))
THREADS = int(os.environ.get("LONG_THREADS", str(os.cpu_count() or 8)))
PROFILE = os.environ.get("LONG_PROFILE", "mixed")
KEYSPACE = int(os.environ.get("LONG_KEYSPACE", "1000000"))
BUCKETS = int(os.environ.get("LONG_BUCKETS", "1024"))
SEED = int(os.environ.get("LONG_SEED", "50000"))
PRIORITY = os.environ.get("LONG_PRIORITY", "normal").lower()


def creation_flags():
    if os.name != "nt":
        return 0
    if PRIORITY == "high":
        return subprocess.HIGH_PRIORITY_CLASS
    if PRIORITY == "above_normal":
        return subprocess.ABOVE_NORMAL_PRIORITY_CLASS
    return subprocess.NORMAL_PRIORITY_CLASS


def last_snapshot_line(path):
    if not os.path.isfile(path):
        return None
    try:
        with open(path, newline="", encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
        return rows[-1] if rows else None
    except OSError:
        return None


def print_progress(impl, row):
    if not row:
        return
    elapsed = row.get("t_elapsed_sec", "?")
    ops_ps = row.get("ops_per_sec_window", "?")
    mem_mib = "?"
    try:
        mem_mib = f"{int(row['mem_ws_bytes']) / 1024.0 / 1024.0:.1f}"
    except Exception:
        pass
    verr = row.get("verify_errors", "?")
    print(
        f"[{impl}] t={elapsed}s, window_ops/s={ops_ps}, mem_ws={mem_mib} MiB, verify_errors={verr}",
        flush=True,
    )


def main():
    if not os.path.isfile(BENCH):
        print("Build bench.exe first (build.bat). Root:", ROOT, file=sys.stderr)
        sys.exit(1)
    os.makedirs(OUT_DIR, exist_ok=True)
    print(
        f"Long run config: seconds={SECONDS}, threads={THREADS}, profile={PROFILE}, "
        f"keyspace={KEYSPACE}, buckets={BUCKETS}, interval={SNAPSHOT_INTERVAL_SEC}, priority={PRIORITY}",
        flush=True,
    )

    for impl in IMPLS:
        out_csv = os.path.join(OUT_DIR, f"snapshots_long_{impl}.csv")
        cmd = [
            BENCH,
            "--impl",
            impl,
            "--threads",
            str(THREADS),
            "--seconds",
            str(SECONDS),
            "--profile",
            PROFILE,
            "--keyspace",
            str(KEYSPACE),
            "--buckets",
            str(BUCKETS),
            "--seed",
            str(SEED),
            "--snapshot-interval-sec",
            str(SNAPSHOT_INTERVAL_SEC),
            "--snapshot-file",
            out_csv,
            "--snapshot-csv-header",
        ]
        print("Running:", " ".join(cmd), flush=True)
        p = subprocess.Popen(
            cmd,
            cwd=ROOT,
            creationflags=creation_flags(),
        )
        deadline = time.time() + max(600, SECONDS + 600)
        last_elapsed = None
        while True:
            rc = p.poll()
            row = last_snapshot_line(out_csv)
            if row and row.get("t_elapsed_sec") != last_elapsed:
                last_elapsed = row.get("t_elapsed_sec")
                print_progress(impl, row)
            if rc is not None:
                if rc != 0:
                    sys.exit(rc)
                break
            if time.time() > deadline:
                p.kill()
                print(f"Timeout while running {impl}", file=sys.stderr)
                sys.exit(1)
            time.sleep(max(1, min(5, SNAPSHOT_INTERVAL_SEC)))
        print("Wrote", out_csv, flush=True)

    print("Plots: python scripts/make_snapshot_plots.py")


if __name__ == "__main__":
    main()
