#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
export CMAKE_BUILD_PARALLEL_LEVEL=2 PIP_DISABLE_PIP_VERSION_CHECK=1
root=/home/gwatts/atlas/correctionlib-feature-build
prefix=$(python -c 'import correctionlib,pathlib; print(pathlib.Path(correctionlib.__file__).parent)')
cmake -S tests/batch_provider -B "$root/batch-probe" -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS=-march=native -ffp-contract=off' > evidence/batches/provider.log 2>&1
cmake --build "$root/batch-probe" -j2 >> evidence/batches/provider.log 2>&1
export BATCH_PROBE_LIBRARY="$root/batch-probe/libbatch_probe.so"
cmake -S . -B "$root/batches" -DBATCH_PROBE_LIBRARY="$BATCH_PROBE_LIBRARY" >> evidence/batches/provider.log 2>&1
ctest --test-dir "$root/batches" --output-on-failure > evidence/batches/native-tests.log 2>&1
python -m pip wheel --no-deps --no-build-isolation examples/node_plugin -w "$root/example-wheels" -Cbuild-dir="$root/example-batches" -Ccmake.define.CMAKE_PREFIX_PATH="$prefix" > evidence/batches/example-build.log 2>&1
python -m pip install --no-index --no-deps --force-reinstall "$root"/example-wheels/*.whl >> evidence/batches/example-build.log 2>&1
python -m pytest -q tests --ignore=tests/test_cvmfs.py > evidence/batches/python-tests.log 2>&1
