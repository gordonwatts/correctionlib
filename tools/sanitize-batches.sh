#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
root=/home/gwatts/atlas/correctionlib-feature-build
flags='-O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer'
cmake -S . -B "$root/sanitize-batches" -DCORRECTIONLIB_BUILD_PYTHON=OFF -DBUILD_DEMO=OFF -DCORRECTIONLIB_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_INSTALL_PREFIX="$root/sanitize-install" > evidence/batches/sanitizer-build.log 2>&1
cmake --build "$root/sanitize-batches" -j2 >> evidence/batches/sanitizer-build.log 2>&1
cmake --install "$root/sanitize-batches" >> evidence/batches/sanitizer-build.log 2>&1
cmake -S tests/batch_provider -B "$root/sanitize-probe" -DCMAKE_PREFIX_PATH="$root/sanitize-install/correctionlib" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="$flags" >> evidence/batches/sanitizer-build.log 2>&1
cmake --build "$root/sanitize-probe" -j2 >> evidence/batches/sanitizer-build.log 2>&1
cmake -S . -B "$root/sanitize-batches" -DBATCH_PROBE_LIBRARY="$root/sanitize-probe/libbatch_probe.so" >> evidence/batches/sanitizer-build.log 2>&1
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir "$root/sanitize-batches" --output-on-failure > evidence/batches/sanitizer-tests.log 2>&1
