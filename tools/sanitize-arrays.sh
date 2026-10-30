#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
root=/home/gwatts/atlas/correctionlib-feature-build
flags='-O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer'
# Reuse this task's native build cache, never the background study installation.
cmake -S . -B "$root/sanitize-batches" -DCORRECTIONLIB_BUILD_PYTHON=OFF -DBUILD_DEMO=OFF -DCORRECTIONLIB_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_INSTALL_PREFIX="$root/sanitize-install" -DARRAY_PROVIDER_LIBRARY= > evidence/arrays/sanitizer-build.log 2>&1
cmake --build "$root/sanitize-batches" -j2 >> evidence/arrays/sanitizer-build.log 2>&1
cmake --install "$root/sanitize-batches" >> evidence/arrays/sanitizer-build.log 2>&1
cmake -S examples/array_plugin -B "$root/sanitize-array-provider" -DCMAKE_PREFIX_PATH="$root/sanitize-install/correctionlib" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="$flags" >> evidence/arrays/sanitizer-build.log 2>&1
cmake --build "$root/sanitize-array-provider" -j2 >> evidence/arrays/sanitizer-build.log 2>&1
cmake -S . -B "$root/sanitize-batches" -DARRAY_PROVIDER_LIBRARY="$root/sanitize-array-provider/libarray_correction_nodes.so" >> evidence/arrays/sanitizer-build.log 2>&1
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir "$root/sanitize-batches" --output-on-failure > evidence/arrays/sanitizer-tests.log 2>&1
