import copy
import json
import pickle
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from importlib.metadata import entry_points
from types import SimpleNamespace

import numpy as np
import pytest
from pydantic import ValidationError

import correctionlib
from correctionlib import _core, plugins, schemav2


@pytest.fixture
def payload():
    return {
        "schema_version": 2,
        "corrections": [
            {
                "name": "probe",
                "version": 1,
                "inputs": [{"name": "x", "type": "real"}],
                "output": {"name": "y", "type": "real"},
                "data": {
                    "nodetype": "extension",
                    "type": "org.example.scale",
                    "version": 1,
                    "config": {"input": "x", "factor": 2.0},
                },
            }
        ],
    }


@pytest.fixture
def provider():
    if not entry_points(group="correctionlib.nodes", name="org.example.scale"):
        pytest.skip(
            "Install examples/node_plugin against this core to run provider integration tests"
        )


def evaluate(payload):
    return correctionlib.CorrectionSet.from_string(json.dumps(payload))["probe"]


def test_missing_plugin(payload):
    payload["corrections"][0]["data"]["type"] = "org.missing.node"
    with pytest.raises(ValueError, match="Missing native plugin"):
        evaluate(payload)


def test_schema_export_without_plugins():
    schema = schemav2.CorrectionSet.model_json_schema()
    assert "Extension" in schema["$defs"]


def test_entry_points_scalar_array_schema_pickle(provider, payload):
    corr = evaluate(payload)
    assert corr.evaluate(3.0) == 6.0
    np.testing.assert_array_equal(
        corr.evaluate(np.array([1.0, 2.0, 3.0])), [2.0, 4.0, 6.0]
    )
    assert _core.has_plugin("org.example.scale", 1)
    assert pickle.loads(pickle.dumps(corr)).evaluate(4.0) == 8.0
    model = schemav2.CorrectionSet.model_validate(payload)
    assert model.to_evaluator()["probe"].evaluate(3.0) == 6.0
    assert (
        schemav2.CorrectionSet.model_validate_json(model.model_dump_json())
        .corrections[0]
        .data.type
        == "org.example.scale"
    )
    counts, stats = model.corrections[0].summary()
    assert counts["Extension"] == 1
    assert stats["x"].unknown
    with ThreadPoolExecutor(4) as pool:
        assert list(pool.map(corr.evaluate, [3.0] * 100)) == [6.0] * 100


def test_nested_and_transform(provider, payload):
    scale = copy.deepcopy(payload["corrections"][0]["data"])
    offset = {
        "nodetype": "extension",
        "type": "org.example.offset",
        "version": 1,
        "config": {"amount": 3.0},
        "children": [scale],
    }
    payload["corrections"][0]["data"] = offset
    assert evaluate(payload).evaluate(2.0) == 7.0
    offset["children"] = [
        {
            "nodetype": "formula",
            "expression": "x*x",
            "parser": "TFormula",
            "variables": ["x"],
        }
    ]
    assert evaluate(payload).evaluate(2.0) == 7.0
    payload["corrections"][0]["data"] = {
        "nodetype": "transform",
        "input": "x",
        "rule": 5.0,
        "content": scale,
    }
    assert evaluate(payload).evaluate(2.0) == 10.0
    payload["corrections"][0]["data"] = {
        "nodetype": "binning",
        "input": "x",
        "edges": [0.0, 10.0],
        "flow": "error",
        "content": [scale],
    }
    assert evaluate(payload).evaluate(2.0) == 4.0
    schemav2.CorrectionSet.model_validate(payload)


@pytest.mark.parametrize(
    "change", ["missing", "extra", "type", "version", "input_type", "children"]
)
def test_invalid_contracts(provider, payload, change):
    node = payload["corrections"][0]["data"]
    if change == "missing":
        del node["config"]["factor"]
    elif change == "extra":
        node["config"]["extra"] = 1
    elif change == "type":
        node["config"]["factor"] = "bad"
    elif change == "version":
        node["version"] = 999
    elif change == "input_type":
        payload["corrections"][0]["inputs"][0]["type"] = "int"
    elif change == "children":
        node["children"] = [1.0]
    with pytest.raises((ValueError, RuntimeError, ValidationError)):
        evaluate(payload)
    with pytest.raises((ValueError, ValidationError)):
        schemav2.CorrectionSet.model_validate(payload)
    # Direct native loading cannot rely on Pydantic.
    with pytest.raises(RuntimeError):
        _core.CorrectionSet.from_string(json.dumps(payload))


def test_spawn_rediscovery(provider, payload):
    code = "import json,sys,correctionlib; c=correctionlib.CorrectionSet.from_string(sys.argv[1]); assert c['probe'].evaluate(3.)==6."
    subprocess.run([sys.executable, "-c", code, json.dumps(payload)], check=True)


def test_cli_validation(provider, payload, tmp_path):
    path = tmp_path / "payload.json"
    path.write_text(json.dumps(payload))
    subprocess.run(
        [sys.executable, "-m", "correctionlib.cli", "validate", str(path)], check=True
    )


def test_discovery_disabled_in_fresh_process(provider, payload):
    code = "import sys,correctionlib; from correctionlib.plugins import enable_discovery; enable_discovery(False); correctionlib.CorrectionSet.from_string(sys.argv[1])"
    result = subprocess.run(
        [sys.executable, "-c", code, json.dumps(payload)],
        capture_output=True,
        text=True,
    )
    assert result.returncode != 0
    assert "Missing native plugin" in result.stderr


def test_duplicate_providers(payload, monkeypatch):
    monkeypatch.setattr(
        plugins,
        "entry_points",
        lambda **kwargs: [
            SimpleNamespace(name="org.duplicate.node"),
            SimpleNamespace(name="org.duplicate.node"),
        ],
    )
    payload["corrections"][0]["data"]["type"] = "org.duplicate.node"
    with pytest.raises(ValueError, match="Duplicate entry-point"):
        evaluate(payload)
