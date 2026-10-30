"""Full Python calls, matched batch-array versus Python point-call loop."""

import csv
import json
import os
import time
from pathlib import Path

import numpy as np

import correctionlib
from correctionlib import _core

_core.load_plugin(os.environ["ARRAY_PROVIDER_LIBRARY"])
rows = []
for fixture in ["histogram", "plugin"]:
    for size in [1, 2, 16]:
        if fixture == "plugin":
            node = {
                "nodetype": "extension",
                "type": "org.example.components",
                "version": 1,
                "output_shape": [size],
                "config": {"input": "x", "size": size},
            }
        else:
            node = {
                "nodetype": "binning",
                "input": "x",
                "edges": [0.0, 1.0, 2.0],
                "content": [
                    {
                        "nodetype": "array",
                        "content": [float(j + base) for j in range(size)],
                    }
                    for base in [3, 7]
                ],
                "flow": "clamp",
            }
        data = {
            "schema_version": 3,
            "corrections": [
                {
                    "name": "h",
                    "version": 1,
                    "inputs": [{"name": "x", "type": "real"}],
                    "output": {"name": "y", "type": "real", "shape": [size]},
                    "data": node,
                }
            ],
        }
        c = correctionlib.CorrectionSet.from_string(json.dumps(data))["h"]
        for n in list(range(1, 17)) + [1000, 10000, 100000]:
            x = (np.arange(n) % 300) * 0.01
            expected = np.stack([c.evaluate(float(v)) for v in x])
            calls = {
                "batch_array": lambda: c.evaluate(x),
                "python_point_loop": lambda: np.stack(
                    [c.evaluate(float(v)) for v in x]
                ),
            }
            for round in range(7):
                for name in list(calls)[:: 1 if round % 2 else -1]:
                    fn = calls[name]
                    np.testing.assert_array_equal(fn(), expected)
                    repeats = max(1, min(50, 5000 // (n * size)))
                    start = time.perf_counter_ns()
                    for _ in range(repeats):
                        out = fn()
                    elapsed = time.perf_counter_ns() - start
                    np.testing.assert_array_equal(out, expected)
                    rows.append(
                        dict(
                            fixture=fixture,
                            n=n,
                            s=size,
                            round=round,
                            mode=name,
                            repeats=repeats,
                            ns_per_call=elapsed / repeats,
                        )
                    )
with Path("evidence/combined/python-raw.csv").open("w") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
