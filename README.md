# Lock-free vs locked key-value benchmark (Windows)

Microbenchmark comparing several in-memory key-value implementations under configurable thread counts and workloads. Outputs CSV and matplotlib plots.

## Requirements

- Windows, C compiler (MSVC Build Tools or MinGW-w64)
- Python 3.10+ with `matplotlib`, `numpy` (see `requirements.txt`)

## Build

```bat
build.bat
```

See `BUILD.txt` for MSVC vs MinGW notes.

## Run full matrix

```bat
run_benchmarks_only.bat
```

Or:

```bat
python scripts\run_all_benchmarks.py
```

Produces `results.csv` and `output\*.png` via `make_plots.py` (invoked by the batch script).

## Optional: long snapshots

```bat
python scripts\run_long_snapshots.py
python scripts\make_snapshot_plots.py
```

## Validate CSV

```bat
python validate_benchmarks.py results.csv
```

## Tests

```bat
pip install -r requirements-dev.txt
pytest
```

## License

MIT — see `LICENSE`.
