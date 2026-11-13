import gzip
import sys

import pytest

import correctionlib._core as core


@pytest.mark.parametrize("reader", ["string", "file", "gzip"])
def test_constant_full_precision(reader, tmp_path):
    # Keep the decimal text literal: serializing a Python float could change it.
    payload = """{
        "schema_version": 2,
        "corrections": [{
            "name": "precision", "version": 1, "inputs": [],
            "output": {"name": "value", "type": "real"},
            "data": 101.32762799999999
        }]
    }"""
    if reader == "string":
        cset = core.CorrectionSet.from_string(payload)
    else:
        if reader == "gzip":
            if sys.platform.startswith("win"):
                pytest.skip("gzip reader requires a build with ZLIB")
            path = tmp_path / "precision.json.gz"
            with gzip.open(path, "wt") as stream:
                stream.write(payload)
        else:
            path = tmp_path / "precision.json"
            path.write_text(payload)
        cset = core.CorrectionSet.from_file(str(path))

    # Correctly rounded binary64; the default RapidJSON path is one ULP higher.
    expected = float.fromhex("0x1.954f7db6e503fp+6")
    assert cset["precision"].evaluate() == expected
