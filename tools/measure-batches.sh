#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
root=/home/gwatts/atlas/correctionlib-feature-build
prefix=$(python -c 'import correctionlib,pathlib; print(pathlib.Path(correctionlib.__file__).parent)')
c++ -O3 -DNDEBUG -std=c++17 -march=native -ffp-contract=off tools/bench-batches.cc -I"$prefix/include" -L"$prefix/lib" -Wl,-rpath,"$prefix/lib" -lcorrectionlib -o "$root/bench-batches"
taskset -c 0 "$root/bench-batches" > evidence/batches/native-raw.csv
taskset -c 0 python tools/bench-batches.py > evidence/batches/python-timing-summary.txt
python -m pip freeze > evidence/batches/packages.txt
uname -a > evidence/batches/machine.txt
lscpu >> evidence/batches/machine.txt
c++ --version >> evidence/batches/machine.txt
cp "$root/batches/compile_commands.json" evidence/batches/compile_commands.json
