#!/usr/bin/env python3
import csv
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BENCH = os.path.join(ROOT, "bench.exe")
IMPLS = ["mutex", "rwlock", "skiplock", "lfhash", "lfskip"]
THREADS = int(os.environ.get("VALIDATE_THREADS", str(os.cpu_count() or 8)))
SECONDS = int(os.environ.get("VALIDATE_SECONDS", "3"))
PROFILE = os.environ.get("VALIDATE_PROFILE", "mixed")
KEYSPACE = int(os.environ.get("VALIDATE_KEYSPACE", "200000"))
BUCKETS = int(os.environ.get("VALIDATE_BUCKETS", "1024"))
SEED = int(os.environ.get("VALIDATE_SEED", "20000"))

CSV_FIELDS = [
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


def parse_result_line(line: str) -> dict:
    return next(csv.DictReader([",".join(CSV_FIELDS), line]))


def main():
    if not os.path.isfile(BENCH):
        print("bench.exe not found", file=sys.stderr)
        sys.exit(1)

    st = subprocess.run(
        [BENCH, "--verify-selftest"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=30,
    )
    if st.returncode != 0:
        print(st.stdout)
        print(st.stderr, file=sys.stderr)
        sys.exit(st.returncode)
    print(st.stdout.strip(), flush=True)

    print("wrongget must report verify_errors > 0 ...", flush=True)
    p_wrong = subprocess.run(
        [
            BENCH,
            "--impl",
            "wrongget",
            "--threads",
            "4",
            "--seconds",
            "2",
            "--profile",
            "readheavy",
            "--keyspace",
            str(KEYSPACE),
            "--buckets",
            str(BUCKETS),
            "--seed",
            str(SEED),
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=60,
    )
    if p_wrong.returncode != 0:
        print(p_wrong.stdout)
        print(p_wrong.stderr, file=sys.stderr)
        sys.exit(p_wrong.returncode)
    row_w = parse_result_line(p_wrong.stdout.strip().splitlines()[-1])
    verr_w = int(row_w["verify_errors"])
    if verr_w <= 0:
        print(
            f"wrongget expected verify_errors > 0, got {verr_w}",
            file=sys.stderr,
        )
        sys.exit(4)
    print(f"OK wrongget: verify_errors={verr_w}", flush=True)

    print(
        f"Validation config: threads={THREADS}, seconds={SECONDS}, profile={PROFILE}, "
        f"keyspace={KEYSPACE}, buckets={BUCKETS}",
        flush=True,
    )

    with tempfile.TemporaryDirectory() as tmpdir:
        for impl in IMPLS:
            snap = os.path.join(tmpdir, f"{impl}.csv")
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
                "1",
                "--snapshot-file",
                snap,
                "--snapshot-csv-header",
            ]
            print("Validate:", " ".join(cmd), flush=True)
            p = subprocess.run(
                cmd,
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=max(60, SECONDS + 30),
            )
            if p.returncode != 0:
                print(p.stdout)
                print(p.stderr, file=sys.stderr)
                sys.exit(p.returncode)
            line = p.stdout.strip().splitlines()[-1]
            row = parse_result_line(line)
            if int(row["verify_errors"]) != 0:
                print(f"verify_errors != 0 for {impl}", file=sys.stderr)
                sys.exit(2)
            if not os.path.isfile(snap):
                print(f"snapshot file missing for {impl}", file=sys.stderr)
                sys.exit(3)
            print(
                f"OK {impl}: ops/s={row['ops_per_sec']}, p99_ns={row['p99_ns']}, "
                f"verify_errors={row['verify_errors']}",
                flush=True,
            )

    print("Validation passed.", flush=True)


if __name__ == "__main__":
    main()
