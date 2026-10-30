"""Confirm real earlier SDK providers fail clearly before registration."""

from pathlib import Path

from correctionlib import _core

root = Path("/home/gwatts/atlas/correctionlib-feature-build")
for path in [
    root / "batch-probe/libbatch_probe.so",
    root / "array-provider/libarray_correction_nodes.so",
]:
    try:
        _core.load_plugin(str(path))
    except RuntimeError as exc:
        if "Incompatible plugin API" not in str(exc):
            raise
        print(path, str(exc))
    else:
        raise AssertionError("old provider unexpectedly accepted")
