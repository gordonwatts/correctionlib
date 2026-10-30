#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
root=/home/gwatts/atlas/correctionlib-feature-build
prefix=$(python -c 'import correctionlib,pathlib; print(pathlib.Path(correctionlib.__file__).parent)')
c++ -O3 -DNDEBUG -std=c++17 -march=native -ffp-contract=off tools/bench-arrays.cc -I"$prefix/include" -L"$prefix/lib" -Wl,-rpath,"$prefix/lib" -lcorrectionlib -o "$root/bench-arrays"
taskset -c 0 "$root/bench-arrays" > evidence/arrays/native-raw.csv
taskset -c 0 python tools/bench-arrays.py
python tools/provenance-features.py arrays "$root/arrays"
python -m pip freeze > evidence/arrays/packages.txt
uname -a > evidence/arrays/machine.txt
lscpu >> evidence/arrays/machine.txt
c++ --version >> evidence/arrays/machine.txt
cp "$root/arrays/compile_commands.json" evidence/arrays/compile_commands.json
