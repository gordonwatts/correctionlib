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

Accepted results: 180 Python tests passed; ROOT and Dask-Awkward unavailable (2
skipped); CVMFS tests excluded. Three native suites passed in both Release and
the Python-free ASan/UBSan/float-cast build. The external probe scalar method
throws; its batch callback succeeds.

Native batch, N=100000: median 313.882 us, range 310.358–317.702 us.

Native scalar_loop, N=100000: median 1174.330 us, range 1172.360–1189.920 us.

Full Python batch at N=100000: median 409.859 us (range 395.743–789.112 us). The
Python scalar-call loop median is 213821.343 us; this ratio includes
Python-loop/call/allocation differences and is not a native-dispatch speedup.
Raw CSV files contain all 19 requested sizes and seven rounds. No comparison
with historical grouped studies is made.
