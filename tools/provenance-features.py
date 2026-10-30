"""Retain source/binary identity, independent of mutable build directories."""

import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys

import correctionlib

root = pathlib.Path(__file__).resolve().parents[1]
label = sys.argv[1]
build = pathlib.Path(sys.argv[2])
prefix = pathlib.Path(correctionlib.__file__).parent
files = (
    list((root / "include").glob("*.h"))
    + list((root / "src").glob("*.cc"))
    + list((prefix / "lib").glob("*correctionlib*"))
    + list(prefix.glob("_core*.so"))
)


files += [
    f
    for folder in ["src/correctionlib", "examples", "tests/batch_provider", "tools"]
    for f in (root / folder).rglob("*")
    if f.is_file()
    and f.suffix in {".py", ".cc", ".h", ".sh"}
    and f.name != "version.py"
]
files += list((root / "src").glob("*.h"))
files += [pathlib.Path(arg) for arg in sys.argv[3:] if pathlib.Path(arg).is_file()]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


data = {
    "source_commit": subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True
    ).strip(),
    "dirty": bool(
        subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True)
    ),
    "submodules": subprocess.check_output(
        ["git", "submodule", "status", "--recursive"], cwd=root, text=True
    ),
    "base_plugin_commit": "8dc0b7a8bfe82c15115ff2a5907d4bfa35ee4710",
    "platform": platform.platform(),
    "python": sys.version,
    "affinity": sorted(os.sched_getaffinity(0)),
    "compiler": subprocess.check_output(["c++", "--version"], text=True),
    "build_dir": str(build),
    "sha256": {str(f): sha(f) for f in files},
}
(root / "evidence" / label / "provenance.json").write_text(
    json.dumps(data, indent=2) + "\n"
)
