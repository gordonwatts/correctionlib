#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
root=/home/gwatts/atlas/correctionlib-feature-build
prefix=$(python -c 'import correctionlib,pathlib; print(pathlib.Path(correctionlib.__file__).parent)')
export ARRAY_PROVIDER_LIBRARY="$root/combined-array-provider/libarray_correction_nodes.so"
c++ -O3 -DNDEBUG -std=c++17 -march=native -ffp-contract=off tools/bench-combined.cc -I"$prefix/include" -L"$prefix/lib" -Wl,-rpath,"$prefix/lib" -lcorrectionlib -o "$root/bench-combined"
taskset -c 0 "$root/bench-combined" "$ARRAY_PROVIDER_LIBRARY" > evidence/combined/native-raw.csv
taskset -c 0 python tools/bench-combined.py
python tools/provenance-features.py combined "$root/combined" "$ARRAY_PROVIDER_LIBRARY" "$root/combined-batch-provider/libbatch_probe.so"
python -m pip freeze > evidence/combined/packages.txt
uname -a > evidence/combined/machine.txt
lscpu >> evidence/combined/machine.txt
c++ --version >> evidence/combined/machine.txt
cp "$root/combined/compile_commands.json" evidence/combined/compile_commands.json
