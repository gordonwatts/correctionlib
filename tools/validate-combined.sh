#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
export CMAKE_BUILD_PARALLEL_LEVEL=2 PIP_DISABLE_PIP_VERSION_CHECK=1
root=/home/gwatts/atlas/correctionlib-feature-build
prefix=$(python -c 'import correctionlib,pathlib; print(pathlib.Path(correctionlib.__file__).parent)')
for item in array batch; do
  src=examples/array_plugin
  if [[ "$item" == batch ]]; then src=tests/batch_provider; fi
  cmake -S "$src" -B "$root/combined-$item-provider" -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS=-march=native -ffp-contract=off' >> evidence/combined/providers.log 2>&1
  cmake --build "$root/combined-$item-provider" -j2 >> evidence/combined/providers.log 2>&1
done
export ARRAY_PROVIDER_LIBRARY="$root/combined-array-provider/libarray_correction_nodes.so"
export BATCH_PROBE_LIBRARY="$root/combined-batch-provider/libbatch_probe.so"
cmake -S . -B "$root/combined" -DARRAY_PROVIDER_LIBRARY="$ARRAY_PROVIDER_LIBRARY" -DBATCH_PROBE_LIBRARY="$BATCH_PROBE_LIBRARY" >> evidence/combined/providers.log 2>&1
ctest --test-dir "$root/combined" --output-on-failure > evidence/combined/native-tests.log 2>&1
python -m pip wheel --no-deps --no-build-isolation examples/node_plugin -w "$root/example-wheels-combined" -Cbuild-dir="$root/example-combined" -Ccmake.define.CMAKE_PREFIX_PATH="$prefix" > evidence/combined/example-build.log 2>&1
python -m pip install --no-index --no-deps --force-reinstall "$root"/example-wheels-combined/*.whl >> evidence/combined/example-build.log 2>&1
python -m pytest -q tests --ignore=tests/test_cvmfs.py > evidence/combined/python-tests.log 2>&1
