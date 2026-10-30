#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
root=/home/gwatts/atlas/correctionlib-feature-build
flags='-O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer'
cmake -S . -B "$root/sanitize-batches" -DCORRECTIONLIB_BUILD_PYTHON=OFF -DBUILD_DEMO=OFF -DCORRECTIONLIB_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_INSTALL_PREFIX="$root/sanitize-install" -DARRAY_PROVIDER_LIBRARY= -DBATCH_PROBE_LIBRARY= > evidence/combined/sanitizer-build.log 2>&1
cmake --build "$root/sanitize-batches" -j2 >> evidence/combined/sanitizer-build.log 2>&1
cmake --install "$root/sanitize-batches" >> evidence/combined/sanitizer-build.log 2>&1
for item in array batch; do
  src=examples/array_plugin
  if [[ "$item" == batch ]]; then src=tests/batch_provider; fi
  cmake -S "$src" -B "$root/sanitize-combined-$item" -DCMAKE_PREFIX_PATH="$root/sanitize-install/correctionlib" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="$flags" >> evidence/combined/sanitizer-build.log 2>&1
  cmake --build "$root/sanitize-combined-$item" -j2 >> evidence/combined/sanitizer-build.log 2>&1
done
cmake -S . -B "$root/sanitize-batches" -DARRAY_PROVIDER_LIBRARY="$root/sanitize-combined-array/libarray_correction_nodes.so" -DBATCH_PROBE_LIBRARY="$root/sanitize-combined-batch/libbatch_probe.so" >> evidence/combined/sanitizer-build.log 2>&1
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir "$root/sanitize-batches" --output-on-failure > evidence/combined/sanitizer-tests.log 2>&1
