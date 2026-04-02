#!/usr/bin/env python3
import csv
import os
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BENCH = os.path.join(ROOT, "bench.exe")
OUT = os.path.join(ROOT, "results.csv")

IMPLS = ["mutex", "rwlock", "skiplock", "lfhash", "lfskip"]
PROFILES = ["readheavy", "mixed", "writeheavy", "churn"]
THREADS = [1, 2, 4, 8, 16, 32, 64]
KEYSPACE = 1_000_000
BUCKETS = 1024
SEED = 50_000

_TOTAL_RUNS = len(IMPLS) * len(PROFILES) * len(THREADS)
_TM = os.environ.get("BENCH_TOTAL_MINUTES", "").strip()
if _TM:
    try:
        SECONDS = max(1, int(float(_TM) * 60.0 / _TOTAL_RUNS))
    except ValueError:
        SECONDS = int(os.environ.get("BENCH_SECONDS", "8"))
else:
    SECONDS = int(os.environ.get("BENCH_SECONDS", "8"))


def _env_truthy(name: str) -> bool:
    return os.environ.get(name, "").strip().lower() in ("1", "true", "yes", "on")


def _sync_csv(f):
    f.flush()
    try:
        os.fsync(f.fileno())
    except OSError:
        pass


def _run_bench_one(cmd, timeout_sec, visible):
    if not visible:
        p = subprocess.run(
            cmd,
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=timeout_sec,
        )
        if p.returncode != 0:
            return None, p
        line = p.stdout.strip().splitlines()[-1]
        return line, p

    proc = subprocess.Popen(
        cmd,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    buf = []

    def _reader():
        try:
            for line in proc.stdout:
                buf.append(line)
                sys.stdout.write(line)
                sys.stdout.flush()
        except Exception:
            pass

    th = threading.Thread(target=_reader, daemon=True)
    th.start()
    try:
        proc.wait(timeout=timeout_sec)
    except subprocess.TimeoutExpired:
        proc.kill()
        try:
            proc.wait(timeout=10)
        except Exception:
            pass
        raise
    th.join(timeout=5)
    if proc.returncode != 0:
        return None, proc
    text = "".join(buf)
    lines = text.strip().splitlines()
    if not lines:
        return None, proc
    return lines[-1], proc


HEADER = [
    "impl",
    "threads",
    "profile",
    "seconds",
    "keyspace",
    "ops_total",
    "ops_per_sec",
    "mem_ws_before",
    "mem_ws_after",
    "mem_delta_ws",
    "p99_ns",
    "verify_errors",
]


def main():
    if not os.path.isfile(BENCH):
        print("Build bench.exe first (build.bat). Root:", ROOT, file=sys.stderr)
        sys.exit(1)

    visible = _env_truthy("BENCH_VISIBLE")
    print(
        f"Matrix: {_TOTAL_RUNS} runs x {SECONDS}s each "
        f"(~{_TOTAL_RUNS * SECONDS // 60} min bench time). "
        f"Stream bench output: {visible}. Ctrl+C to stop.",
        flush=True,
    )

    run_i = 0
    try:
        with open(OUT, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(HEADER)
            _sync_csv(f)
            for impl in IMPLS:
                for prof in PROFILES:
                    for thr in THREADS:
                        run_i += 1
                        cmd = [
                            BENCH,
                            "--impl",
                            impl,
                            "--threads",
                            str(thr),
                            "--seconds",
                            str(SECONDS),
                            "--profile",
                            prof,
                            "--keyspace",
                            str(KEYSPACE),
                            "--buckets",
                            str(BUCKETS),
                            "--seed",
                            str(SEED),
                        ]
                        t0 = time.perf_counter()
                        to = max(120, SECONDS + 120)
                        print(
                            f"\n--- [{run_i}/{_TOTAL_RUNS}] {' '.join(cmd[1:])} ---",
                            flush=True,
                        )
                        try:
                            line, proc = _run_bench_one(cmd, to, visible)
                        except subprocess.TimeoutExpired:
                            print("bench timeout.", file=sys.stderr, flush=True)
                            sys.exit(124)
                        rc = getattr(proc, "returncode", 0)
                        if rc != 0 or line is None:
                            print(
                                f"bench exit code {rc}",
                                file=sys.stderr,
                                flush=True,
                            )
                            sys.exit(rc if rc else 1)
                        w.writerow(line.split(","))
                        _sync_csv(f)
                        dt = time.perf_counter() - t0
                        verr = line.split(",")[-1] if line.count(",") >= 11 else "?"
                        print(
                            f"OK [{run_i}/{_TOTAL_RUNS}] verify_errors={verr} "
                            f"({dt:.1f}s wall)",
                            flush=True,
                        )
    except KeyboardInterrupt:
        print("\nInterrupted. results.csv may be incomplete.", flush=True)
        sys.exit(130)

    print("Wrote", OUT)


if __name__ == "__main__":
    main()
