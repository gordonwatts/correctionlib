import json
import os
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import pytest

import correctionlib
from correctionlib import _core


def make(node, inputs=None):
    return correctionlib.CorrectionSet.from_string(
        json.dumps(
            {
                "schema_version": 2,
                "corrections": [
                    {
                        "name": "p",
                        "version": 1,
                        "inputs": inputs or [{"name": "x", "type": "real"}],
                        "output": {"name": "y", "type": "real"},
                        "data": node,
                    }
                ],
            }
        )
    )["p"]


@pytest.mark.parametrize("n", list(range(1, 17)) + [0, 1000, 10000, 100000])
@pytest.mark.parametrize("flow", ["clamp", "wrap", -3.0])
@pytest.mark.parametrize("multi", [False, True])
def test_histogram_batch(n, flow, multi):
    node = {
        "nodetype": "binning",
        "input": "x",
        "edges": [0.0, 1.0, 2.0],
        "content": [3.0, 7.0],
        "flow": flow,
    }
    if multi:
        node.update(nodetype="multibinning", inputs=["x"], edges=[node["edges"]])
        del node["input"]
    c = make(node)
    x = np.resize(np.array([-1.0, 0.0, np.nextafter(1.0, 0), 1.0, 2.0, 3.0]), n)
    expected = np.array([c.evaluate(float(v)) for v in x])
    np.testing.assert_array_equal(c.evaluate(x[::-1]), expected[::-1])
    if flow != "wrap":
        for v in [-np.inf, np.inf, np.nan]:
            np.testing.assert_array_equal(
                c.evaluate(np.array([v])), [c.evaluate(float(v))]
            )


def test_nested_fallback():
    c = make(
        {
            "nodetype": "transform",
            "input": "x",
            "rule": {
                "nodetype": "formula",
                "parser": "TFormula",
                "expression": "x+1",
                "variables": ["x"],
            },
            "content": {
                "nodetype": "binning",
                "input": "x",
                "edges": [0.0, 1.0, 2.0],
                "content": [3.0, 7.0],
                "flow": "clamp",
            },
        }
    )
    x = np.arange(20, dtype=np.float32)[::2]
    np.testing.assert_array_equal(c.evaluate(x), [c.evaluate(float(v)) for v in x])
    with ThreadPoolExecutor(4) as pool:
        for out in pool.map(c.evaluate, [x] * 8):
            np.testing.assert_array_equal(out, c.evaluate(x))


def test_provider_dispatch():
    path = os.environ.get("BATCH_PROBE_LIBRARY")
    if not path:
        pytest.skip("set BATCH_PROBE_LIBRARY to independently built probe")
    _core.load_plugin(path)
    c = make(
        {"nodetype": "extension", "type": "test.batch", "version": 1, "config": {}}
    )
    with pytest.raises(RuntimeError, match="scalar probe reached"):
        c.evaluate(1.0)
    for n in list(range(17)) + [1000, 10000, 100000]:
        x = np.arange(n, dtype=np.float32)[::-1]
        np.testing.assert_array_equal(c.evaluate(x), x.astype(float) * 2)
    with pytest.raises(ValueError):
        c._base.evalv(np.zeros((2, 2)))


@pytest.mark.parametrize("kind", ["int", "real"])
def test_broadcast_and_types(kind):
    c = make(
        {
            "nodetype": "category",
            "input": "label",
            "content": [
                {
                    "key": "a",
                    "value": {
                        "nodetype": "formula",
                        "parser": "TFormula",
                        "expression": "x+y",
                        "variables": ["x", "y"],
                    },
                }
            ],
        },
        [
            {"name": "x", "type": "real"},
            {"name": "y", "type": "real"},
            {"name": "label", "type": "string"},
        ],
    )
    x = np.arange(6.0).reshape(2, 3)[:, ::-1]
    y = np.arange(2.0)[:, None]
    np.testing.assert_array_equal(c.evaluate(x, y, "a"), x + y)
    with pytest.raises(ValueError, match="same size"):
        c._base.evalv(np.zeros(2), np.zeros(3), "a")
    with pytest.raises(ValueError, match="only allowed"):
        c._base.evalv(np.zeros(2), 1.0, np.array(["a", "a"]))
    node = {
        "nodetype": "binning",
        "input": "x",
        "edges": {"n": 2, "low": 0.0, "high": 2.0},
        "content": [3.0, 7.0],
        "flow": "error",
    }
    c = make(node, [{"name": "x", "type": kind}])
    dtype = np.int64 if kind == "int" else np.float64
    np.testing.assert_array_equal(c.evaluate(np.array([0, 1], dtype=dtype)), [3.0, 7.0])
    for x in [-1, 2]:
        value = x if kind == "int" else float(x)
        with pytest.raises(RuntimeError, match="bounds"):
            c.evaluate(np.array([value], dtype=dtype))


@pytest.mark.parametrize("flow", ["clamp", "wrap", "error", -3.0])
def test_uniform_nonfinite(flow):
    c = make(
        {
            "nodetype": "binning",
            "input": "x",
            "edges": {"n": 2, "low": 0.0, "high": 2.0},
            "content": [3.0, 7.0],
            "flow": flow,
        }
    )
    with pytest.raises(RuntimeError):
        c.evaluate(np.array([np.nan]))
    for value in [-np.inf, np.inf]:
        if flow in ("wrap", "error"):
            with pytest.raises(RuntimeError):
                c.evaluate(np.array([value]))
        else:
            np.testing.assert_array_equal(
                c.evaluate(np.array([value])), [c.evaluate(value)]
            )
