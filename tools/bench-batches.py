"""Full high-level Python call versus a Python scalar-call loop; no rescaling."""

import csv
import json
import statistics
import time
from pathlib import Path

import numpy as np

import correctionlib

c = correctionlib.CorrectionSet.from_string(
    json.dumps(
        {
            "schema_version": 2,
            "corrections": [
                {
                    "name": "h",
                    "version": 1,
                    "inputs": [{"name": "x", "type": "real"}],
                    "output": {"name": "y", "type": "real"},
                    "data": {
                        "nodetype": "binning",
                        "input": "x",
                        "edges": [0.0, 1.0, 2.0],
                        "content": [3.0, 7.0],
                        "flow": "clamp",
                    },
                }
            ],
        }
    )
)["h"]
rows = []
for n in list(range(1, 17)) + [1000, 10000, 100000]:
    x = (np.arange(n) % 300) * 0.01
    expected = np.array([c.evaluate(float(v)) for v in x])
    calls = {
        "batch": lambda: c.evaluate(x),
        "python_scalar_loop": lambda: np.array([c.evaluate(float(v)) for v in x]),
    }
    for round in range(7):
        for name in list(calls)[:: 1 if round % 2 else -1]:
            fn = calls[name]
            np.testing.assert_array_equal(fn(), expected)
            repeats = max(1, min(100, 10000 // n))
            start = time.perf_counter_ns()
            for _ in range(repeats):
                out = fn()
            elapsed = time.perf_counter_ns() - start
            np.testing.assert_array_equal(out, expected)
            rows.append(
                dict(
                    n=n,
                    round=round,
                    mode=name,
                    repeats=repeats,
                    ns_per_call=elapsed / repeats,
                )
            )
path = Path("evidence/batches/python-raw.csv")
with path.open("w") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
for mode in calls:
    r = [v["ns_per_call"] for v in rows if v["mode"] == mode and v["n"] == 100000]
    print(mode, "N=100000 median ns", statistics.median(r), "min/max", min(r), max(r))
