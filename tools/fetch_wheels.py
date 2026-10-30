"""Download Python 3.13 wheels using AL9's system Python and working TLS."""

import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from urllib.request import urlopen

destination = Path(sys.argv[1]).resolve()
destination.mkdir(parents=True, exist_ok=True)
# pip 25.3 still runs on the system Python 3.9. Avoid the bootstrap host and
# obtain the pinned, hash-checked wheel directly from PyPI's public metadata.
with urlopen("https://pypi.org/pypi/pip/25.3/json", timeout=30) as response:
    metadata = json.load(response)
entry = next(item for item in metadata["urls"] if item["filename"].endswith(".whl"))
with urlopen(entry["url"], timeout=30) as response:
    data = response.read()
if hashlib.sha256(data).hexdigest() != entry["digests"]["sha256"]:
    raise RuntimeError("pip wheel hash mismatch")
wheel = destination / entry["filename"]
wheel.write_bytes(data)
environment = {
    **os.environ,
    "PYTHONPATH": str(wheel),
    "PIP_DISABLE_PIP_VERSION_CHECK": "1",
}
subprocess.run(
    [
        sys.executable,
        "-m",
        "pip",
        "download",
        "--only-binary=:all:",
        "--python-version",
        "3.13",
        "--abi",
        "cp313",
        "--dest",
        str(destination),
        *sys.argv[2:],
    ],
    env=environment,
    check=True,
)
