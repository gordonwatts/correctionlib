#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
export CMAKE_BUILD_PARALLEL_LEVEL=2 PIP_DISABLE_PIP_VERSION_CHECK=1
root=/home/gwatts/atlas/correctionlib-feature-build
python -m pip wheel --no-deps --no-build-isolation . -w "$root/wheels-arrays" -v -Cbuild-dir="$root/arrays" -Ccmake.define.CORRECTIONLIB_BUILD_TESTS=ON -Ccmake.define.CMAKE_EXPORT_COMPILE_COMMANDS=ON '-Ccmake.define.CMAKE_CXX_FLAGS=-march=native -ffp-contract=off' > evidence/arrays/build.log 2>&1
wheel=$(python -c 'from pathlib import Path; import sys; print(max(Path(sys.argv[1]).glob("*.whl"), key=lambda p: p.stat().st_mtime))' "$root/wheels-arrays")
python -m pip install --no-index --no-deps --force-reinstall "$wheel"
