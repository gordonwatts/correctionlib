#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
export CMAKE_BUILD_PARALLEL_LEVEL=2 PIP_DISABLE_PIP_VERSION_CHECK=1
root=/home/gwatts/atlas/correctionlib-feature-build
prefix=$(python -c 'import correctionlib,pathlib; print(pathlib.Path(correctionlib.__file__).parent)')
cmake -S examples/array_plugin -B "$root/array-provider" -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS=-march=native -ffp-contract=off' > evidence/arrays/provider.log 2>&1
cmake --build "$root/array-provider" -j2 >> evidence/arrays/provider.log 2>&1
export ARRAY_PROVIDER_LIBRARY="$root/array-provider/libarray_correction_nodes.so"
cmake -S . -B "$root/arrays" -DARRAY_PROVIDER_LIBRARY="$ARRAY_PROVIDER_LIBRARY" >> evidence/arrays/provider.log 2>&1
ctest --test-dir "$root/arrays" --output-on-failure > evidence/arrays/native-tests.log 2>&1
python -m pip wheel --no-deps --no-build-isolation examples/node_plugin -w "$root/example-wheels-arrays" -Cbuild-dir="$root/example-arrays" -Ccmake.define.CMAKE_PREFIX_PATH="$prefix" > evidence/arrays/example-build.log 2>&1
python -m pip install --no-index --no-deps --force-reinstall "$root"/example-wheels-arrays/*.whl >> evidence/arrays/example-build.log 2>&1
python -m pytest -q tests --ignore=tests/test_cvmfs.py > evidence/arrays/python-tests.log 2>&1
