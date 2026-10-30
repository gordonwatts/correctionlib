# Native-batch validation and timing evidence

Run in WSL2 atlas_al9, using ATLAS AnalysisBase 25.2.100 (GCC 15.2, Python
3.13). The development checkout/builds are on the Linux filesystem. The
background study and its pinned dependency were not edited or reinstalled.

```bash
/usr/bin/python3 tools/fetch_wheels.py /home/gwatts/atlas/correctionlib-feature-build/wheels pip==25.3 scikit-build-core setuptools-scm pybind11 numpy pydantic rich packaging pytest ruff awkward scipy pandas uhi pytest-benchmark
bash tools/bootstrap-features.sh
bash tools/validate-batches.sh
bash tools/sanitize-batches.sh
bash tools/measure-batches.sh
```

Run commands from the root of this Linux checkout. The source for each timer is
retained in [tools](../../tools).

The native timer includes the C++ evaluate_batch call into a 2-bin lookup,
writing caller-allocated output. Its scalar control loops over
Correction::evaluate on the same inputs and preallocated output. The Python
timer includes broadcasting, flattening, conversion, output allocation and
evaluation; its control is a Python loop of scalar calls plus result
construction. Neither timer multiplies results by coordinates. These two
controls have different costs and must not be combined into a claim about Python
overhead or batching alone. Seven raw rounds retain alternating order, repeat
counts and nanoseconds per call. Each round checks exact agreement before and
after timing. Timings run on CPU 0 after builds end.

Provenance uses source/header/binary hashes to identify an uncommitted feature
build; source_commit points to the preceding commit and dirty=true records the
pending feature changes. Setup affinity is recorded separately from the timing
processes, which tools/measure-batches.sh pins to CPU0. Earlier accepted rounds
are preserved under revision-a4d3a0c.

Final safety-revision validation: 185 Python tests passed; ROOT unavailable (one
skipped), CVMFS excluded. Release and Python-free ASan/UBSan/float-cast native
suites passed 3/3 with separately built providers. All pinned local hooks passed
except pyupgrade, which was skipped because its current pin requires Python3.10
while the isolated lint interpreter is3.9. The PR CI pinned type checker and
original plugin workflow passed; initial format failures were mechanical
documentation changes. Raw evidence is excluded from formatters to preserve
original bytes.

N=100000, microseconds per call:

| Scope  | Mode               |     Median |               Min–max |
| ------ | ------------------ | ---------: | --------------------: |
| native | batch              |    329.356 |       308.689–361.974 |
| native | scalar_loop        |   1255.140 |     1239.530–1397.670 |
| python | batch              |    419.103 |       364.197–734.100 |
| python | python_scalar_loop | 216756.439 | 215063.388–222384.635 |
