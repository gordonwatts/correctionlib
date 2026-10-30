import json
import os
import pickle
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import pytest

import correctionlib
from correctionlib import _core


def payload(node, size=2, version=3):
    output = {"name": "components", "type": "real"}
    if size is not None:
        output["shape"] = [size]
    return {
        "schema_version": version,
        "corrections": [
            {
                "name": "p",
                "version": 1,
                "inputs": [{"name": "x", "type": "real"}],
                "output": output,
                "data": node,
            }
        ],
    }


def arr(values):
    return {"nodetype": "array", "content": values}


def make(node, size=2):
    return correctionlib.CorrectionSet.from_string(json.dumps(payload(node, size)))["p"]


@pytest.mark.parametrize("size", [1, 2, 16])
@pytest.mark.parametrize("n", list(range(17)) + [1000, 10000, 100000])
def test_shapes(size, n):
    c = make(
        {
            "nodetype": "binning",
            "input": "x",
            "edges": [0.0, 1.0, 2.0],
            "content": [
                arr(list(np.arange(size, dtype=float))),
                arr(list(np.arange(size, dtype=float) + 10)),
            ],
            "flow": "clamp",
        },
        size,
    )
    x = np.resize(
        np.array([-1.0, 0.0, np.nextafter(1.0, 0), 1.0, 2.0, np.inf, -np.inf, np.nan]),
        n,
    )
    expected = np.arange(size)[None, :] + np.where(x < 1, 0, 10)[:, None]
    np.testing.assert_array_equal(
        c.evaluate(x.astype(np.float64)[::-1]), expected[::-1]
    )
    assert c.output_shape == (size,)
    assert c.evaluate(0.25).shape == (size,)
    np.testing.assert_array_equal(c.evaluate(1.25), np.arange(size) + 10)


def test_broadcast_pickle_and_concurrency():
    formula = {
        "nodetype": "formula",
        "expression": "x",
        "parser": "TFormula",
        "variables": ["x"],
    }
    c = make(
        {
            "nodetype": "transform",
            "input": "x",
            "rule": {**formula, "expression": "x+1"},
            "content": arr([formula, 9.0]),
        }
    )
    x = np.arange(12.0).reshape(3, 4)[:, ::-1]
    expected = np.stack([x + 1, np.full_like(x, 9)], axis=-1)
    np.testing.assert_array_equal(c.evaluate(x), expected)
    np.testing.assert_array_equal(pickle.loads(pickle.dumps(c)).evaluate(x), expected)
    assert c.evaluate(np.empty((2, 0))).shape == (2, 0, 2)
    with ThreadPoolExecutor(4) as pool:
        for result in pool.map(c.evaluate, [x] * 8):
            np.testing.assert_array_equal(result, expected)


@pytest.mark.parametrize("flow", ["clamp", "wrap", "error", arr([-3.0, -4.0])])
def test_multibinning_flow(flow):
    node = {
        "nodetype": "multibinning",
        "inputs": ["x"],
        "edges": [[0.0, 1.0, 2.0]],
        "content": [arr([1.0, 2.0]), arr([3.0, 4.0])],
        "flow": flow,
    }
    c = make(node)
    np.testing.assert_array_equal(c.evaluate(np.array([0.0, 1.0])), [[1, 2], [3, 4]])
    if flow == "error":
        with pytest.raises(RuntimeError, match="bounds"):
            c.evaluate(np.array([-1.0]))
    elif isinstance(flow, dict):
        np.testing.assert_array_equal(
            c.evaluate(np.array([-np.inf, np.inf, np.nan])), [[-3, -4]] * 3
        )
    elif flow == "wrap":
        np.testing.assert_array_equal(
            c.evaluate(np.array([-1.0, 2.0])), [[3, 4], [1, 2]]
        )


@pytest.mark.parametrize("size", [0, -1, True, 1.5, "2", None])
def test_bad_shapes(size):
    data = payload(arr([1.0, 2.0]))
    data["corrections"][0]["output"]["shape"] = [size]
    with pytest.raises((RuntimeError, ValueError)):
        _core.CorrectionSet.from_string(json.dumps(data))


@pytest.mark.parametrize(
    "node",
    [
        arr([]),
        arr([arr([1.0])]),
        arr([1.0]),
        {
            "nodetype": "binning",
            "input": "x",
            "edges": [0.0, 1.0],
            "content": [arr([1.0, 2.0])],
            "flow": 0.0,
        },
        {
            "nodetype": "transform",
            "input": "x",
            "rule": arr([1.0, 2.0]),
            "content": arr([1.0, 2.0]),
        },
        1.0,
    ],
)
def test_bad_graph_shapes(node):
    with pytest.raises((RuntimeError, ValueError)):
        _core.CorrectionSet.from_string(json.dumps(payload(node)))


def test_version_and_explicit_cpp_api():
    for version, size in [(2, 2), (2, None), (3, None)]:
        with pytest.raises((RuntimeError, ValueError)):
            _core.CorrectionSet.from_string(
                json.dumps(payload(arr([1.0, 2.0]), size, version))
            )
    c = make(arr([1.0, 2.0]))
    np.testing.assert_array_equal(c._base.evaluate_array(0.0), [1, 2])
    scalar = make(3.0, None)
    assert isinstance(scalar.evaluate(0.0), float)
    with pytest.raises(RuntimeError, match="Scalar correction"):
        scalar._base.evaluate_array(0.0)
    with pytest.raises(TypeError, match="NumPy"):
        import awkward as ak

        c.evaluate(ak.Array([1.0, 2.0]))


def test_schema_authoring():
    from correctionlib import schemav2, schemav3
    from correctionlib.highlevel import model_auto

    data = payload(arr([1.0, 2.0]))
    model = model_auto(json.dumps(data))
    assert isinstance(model, schemav3.CorrectionSet)
    np.testing.assert_array_equal(model.to_evaluator()["p"].evaluate(1.0), [1, 2])
    assert (
        schemav3.CorrectionSet.model_json_schema()["properties"]["schema_version"][
            "const"
        ]
        == 3
    )
    with pytest.raises(ValueError):
        schemav2.CorrectionSet.model_validate(data)
    data["corrections"][0]["data"] = 1.0
    with pytest.raises(ValueError):
        schemav3.CorrectionSet.model_validate(data)


@pytest.mark.parametrize("size", [1, 2, 16])
def test_external_array_provider(size):
    path = os.environ.get("ARRAY_PROVIDER_LIBRARY")
    if not path:
        pytest.skip("set ARRAY_PROVIDER_LIBRARY to independently built provider")
    _core.load_plugin(path)
    node = {
        "nodetype": "extension",
        "type": "org.example.components",
        "version": 1,
        "output_shape": [size],
        "config": {"input": "x", "size": size},
    }
    c = make(node, size)
    for n in list(range(17)) + [1000, 10000, 100000]:
        x = np.arange(n, dtype=np.float32)[::-1]
        np.testing.assert_array_equal(
            c.evaluate(x), x.astype(float)[:, None] + np.arange(size)
        )
    wrapper = {
        "nodetype": "extension",
        "type": "org.example.array_offset",
        "version": 1,
        "output_shape": [size],
        "config": {},
        "children": [node],
    }
    np.testing.assert_array_equal(
        make(wrapper, size).evaluate(0.0), np.arange(size) + 10
    )
    node["output_shape"] = [size + 1]
    with pytest.raises((RuntimeError, ValueError), match="size"):
        make(node, size + 1)


@pytest.mark.parametrize("flow", ["clamp", "wrap", "error", arr([5.0, 6.0])])
def test_uniform_nonfinite(flow):
    c = make(
        {
            "nodetype": "binning",
            "input": "x",
            "edges": {"n": 2, "low": 0.0, "high": 2.0},
            "content": [arr([1.0, 2.0]), arr([3.0, 4.0])],
            "flow": flow,
        }
    )
    with pytest.raises(RuntimeError):
        c.evaluate(np.array([np.nan]))
    for value in [np.inf, -np.inf]:
        if flow in ("wrap", "error"):
            with pytest.raises(RuntimeError):
                c.evaluate(value)
        elif flow == "clamp":
            np.testing.assert_array_equal(
                c.evaluate(value), [3, 4] if value > 0 else [1, 2]
            )
        else:
            np.testing.assert_array_equal(c.evaluate(value), [5, 6])


def test_category_broadcast_and_compound_rejection():
    formula = {
        "nodetype": "formula",
        "parser": "TFormula",
        "expression": "x+y",
        "variables": ["x", "y"],
    }
    node = {
        "nodetype": "category",
        "input": "label",
        "content": [{"key": "selected", "value": arr([formula, 2.0])}],
        "default": arr([0.0, 0.0]),
    }
    data = payload(node)
    data["corrections"][0]["inputs"] += [
        {"name": "y", "type": "real"},
        {"name": "label", "type": "string"},
    ]
    c = correctionlib.CorrectionSet.from_string(json.dumps(data))["p"]
    x, y = np.arange(3.0)[:, None], np.arange(4.0)[None, :]
    expected = np.stack([x + y, np.full((3, 4), 2.0)], axis=-1)
    np.testing.assert_array_equal(c.evaluate(x, y, "selected"), expected)
    data["compound_corrections"] = [
        {
            "name": "bad",
            "inputs": data["corrections"][0]["inputs"],
            "output": {"name": "y", "type": "real"},
            "inputs_update": [],
            "input_op": "*",
            "output_op": "*",
            "stack": ["p"],
        }
    ]
    with pytest.raises(RuntimeError, match="CompoundCorrection"):
        _core.CorrectionSet.from_string(json.dumps(data))


def test_malformed_geometry_and_shape_rank():
    for edges in [[], [0.0], [float("nan"), 1.0], {"n": 1, "low": 1.0, "high": 0.0}]:
        with pytest.raises(RuntimeError):
            make(
                {
                    "nodetype": "binning",
                    "input": "x",
                    "edges": edges,
                    "content": [arr([1.0, 2.0])],
                    "flow": "clamp",
                }
            )
    for shape in [[], [1, 2], None, 2]:
        data = payload(arr([1.0, 2.0]))
        data["corrections"][0]["output"]["shape"] = shape
        with pytest.raises(RuntimeError):
            _core.CorrectionSet.from_string(json.dumps(data))


def test_empty_types():
    c = make(arr([1.0, 2.0]))
    with pytest.raises(RuntimeError):
        c._base.evalv("bad")
