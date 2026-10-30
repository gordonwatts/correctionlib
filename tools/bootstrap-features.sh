#!/usr/bin/env bash
export ATLAS_LOCAL_ROOT_BASE=/cvmfs/atlas.cern.ch/repo/ATLASLocalRootBase
source "$ATLAS_LOCAL_ROOT_BASE/user/atlasLocalSetup.sh" --quiet
asetup AnalysisBase,25.2.100 >/dev/null
unset PYTHONPATH
set -euo pipefail
root=/home/gwatts/atlas/correctionlib-feature-build
mkdir -p "$root"
python -m venv "$root/venv"
source "$root/venv/bin/activate"
python -m pip install --no-index --find-links=/home/gwatts/atlas/correctionlib-feature-build/wheels pip scikit-build-core setuptools-scm pybind11 numpy pydantic rich packaging pytest ruff awkward scipy pandas uhi pytest-benchmark
cd /home/gwatts/atlas/correctionlib_features
python -m pip wheel --no-deps --no-build-isolation . -w "$root/wheels-batches" -v -Cbuild-dir="$root/batches" -Ccmake.define.CORRECTIONLIB_BUILD_TESTS=ON -Ccmake.define.CMAKE_EXPORT_COMPILE_COMMANDS=ON '-Ccmake.define.CMAKE_CXX_FLAGS=-march=native -ffp-contract=off' > evidence/batches/build.log 2>&1
wheel=$(python -c 'from pathlib import Path; import sys; print(max(Path(sys.argv[1]).glob("*.whl"), key=lambda p: p.stat().st_mtime))' "$root/wheels-batches")
python -m pip install --no-index --no-deps --force-reinstall "$wheel"
