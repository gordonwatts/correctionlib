#!/usr/bin/env bash
cd /home/gwatts/atlas/correctionlib_features
source tools/feature-env.sh
set -euo pipefail
root=/home/gwatts/atlas/correctionlib-feature-build
ruff check tools tests/test_batch_array_outputs.py examples/batch_arrays/example.py --fix
ruff format tools tests/test_batch_array_outputs.py examples/batch_arrays/example.py
cmake --build "$root/combined" -j2 > evidence/combined/rebuild-tests.log 2>&1
bash tools/validate-combined.sh
prefix=$(python -c 'import correctionlib,pathlib; print(pathlib.Path(correctionlib.__file__).parent)')
cmake -S examples/batch_arrays -B "$root/combined-example" -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_BUILD_TYPE=Release > evidence/combined/example.log 2>&1
cmake --build "$root/combined-example" -j2 >> evidence/combined/example.log 2>&1
"$root/combined-example/batch_arrays" examples/batch_arrays/payload.json >> evidence/combined/example.log
python examples/batch_arrays/example.py >> evidence/combined/example.log
