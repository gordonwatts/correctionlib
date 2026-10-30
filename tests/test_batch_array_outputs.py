import json
import os
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import pytest

import correctionlib
from correctionlib import _core


def make(size, kind="test.array_batch", wrapper=False):
    path = os.environ.get("ARRAY_PROVIDER_LIBRARY")
    if not path:
        pytest.skip("set ARRAY_PROVIDER_LIBRARY for independent batch-array probe")
    _core.load_plugin(path)
    data = {
        "schema_version": 3,
        "corrections": [
            {
                "name": "p",
                "version": 1,
                "inputs": [{"name": "x", "type": "real"}],
                "output": {"name": "y", "type": "real", "shape": [size]},
                "data": {
                    "nodetype": "extension",
                    "type": kind,
                    "version": 1,
                    "output_shape": [size],
                    "config": {"input": "x", "size": size},
                },
            }
        ],
    }
    if wrapper:
        root = data["corrections"][0]
        root["data"] = {
            "nodetype": "extension",
            "type": "org.example.array_offset",
            "version": 1,
            "output_shape": [size],
            "config": {},
            "children": [root["data"]],
        }
    return correctionlib.CorrectionSet.from_string(json.dumps(data))["p"]


@pytest.mark.parametrize("size", [1, 2, 16])
def test_batch_array_dispatch(size):
    c = make(size)
    with pytest.raises(RuntimeError, match="scalar array probe reached"):
        c.evaluate(1.0)
    for n in list(range(17)) + [1000, 10000, 100000]:
        x = np.arange(n, dtype=np.float32)[::-1]
        np.testing.assert_array_equal(
            c.evaluate(x), x.astype(float)[:, None] + np.arange(size)
        )
    x = np.arange(12.0).reshape(3, 4)[:, ::-1]
    np.testing.assert_array_equal(c.evaluate(x), x[..., None] + np.arange(size))
    assert c.evaluate(np.empty((2, 0))).shape == (2, 0, size)
    with ThreadPoolExecutor(4) as pool:
        for out in pool.map(c.evaluate, [x] * 8):
            np.testing.assert_array_equal(out, x[..., None] + np.arange(size))


def test_array_batch_nonfinite():
    c = make(2, "org.example.components")
    for value in [0.0, np.inf, -np.inf, np.nan]:
        expected = c.evaluate(value)
        np.testing.assert_array_equal(c.evaluate(np.array([value])), expected[None, :])


def test_default_provider_batch_array_fallback():
    c = make(2, "org.example.components", wrapper=True)
    x = np.arange(10.0)[::-1]
    np.testing.assert_array_equal(c.evaluate(x), x[:, None] + np.arange(2) + 10)
