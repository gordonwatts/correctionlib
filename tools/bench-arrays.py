"""Full Python array-output call versus matched scalar component array calls."""

import csv
import json
import time
from pathlib import Path

import numpy as np

import correctionlib

rows = []
for size in [1, 2, 16]:

    def correction(name, component=None):
        def values(base):
            return (
                {"nodetype": "array", "content": [float(base + j) for j in range(size)]}
                if component is None
                else float(base + component)
            )

        output = {"name": "y", "type": "real"}
        if component is None:
            output["shape"] = [size]
        return {
            "name": name,
            "version": 1,
            "inputs": [{"name": "x", "type": "real"}],
            "output": output,
            "data": {
                "nodetype": "binning",
                "input": "x",
                "edges": [0.0, 1.0, 2.0],
                "content": [values(3), values(7)],
                "flow": "clamp",
            },
        }

    data = {
        "schema_version": 3,
        "corrections": [correction("a")] + [correction(str(j), j) for j in range(size)],
    }
    cset = correctionlib.CorrectionSet.from_string(json.dumps(data))
    array = cset["a"]
    scalars = [cset[str(j)] for j in range(size)]
    for n in list(range(1, 17)) + [1000, 10000, 100000]:
        x = (np.arange(n) % 300) * 0.01
        expected = np.stack([c.evaluate(x) for c in scalars], axis=-1)
        calls = {
            "array": lambda: array.evaluate(x),
            "scalar_components": lambda: np.stack(
                [c.evaluate(x) for c in scalars], axis=-1
            ),
        }
        for round in range(7):
            for name in list(calls)[:: 1 if round % 2 else -1]:
                fn = calls[name]
                np.testing.assert_array_equal(fn(), expected)
                repeats = max(1, min(100, 10000 // (n * size)))
                start = time.perf_counter_ns()
                for _ in range(repeats):
                    out = fn()
                elapsed = time.perf_counter_ns() - start
                np.testing.assert_array_equal(out, expected)
                rows.append(
                    dict(
                        n=n,
                        s=size,
                        round=round,
                        mode=name,
                        repeats=repeats,
                        ns_per_call=elapsed / repeats,
                    )
                )
with Path("evidence/arrays/python-raw.csv").open("w") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
