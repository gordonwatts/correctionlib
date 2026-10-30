# Fixed array-output evidence

Builds/tests use WSL2 atlas_al9, AnalysisBase 25.2.100, GCC 15.2 and Python 3.13.
Run commands from this checkout on the Linux filesystem. tools/feature-env.sh
uses the independent venv created for this task, never the background study.

```bash
bash tools/build-arrays.sh
bash tools/validate-arrays.sh
bash tools/sanitize-arrays.sh
bash tools/measure-arrays.sh
```

The native timer compares a loop of explicit evaluate_array calls into a
preallocated row-major output with loops over S matched scalar component graphs.
Both write N*S doubles. The Python timer compares one high-level array-output
call with S high-level scalar-output array calls and stacking their columns.
It includes broadcasting, flattening, conversions and result allocation, but no
coordinate rescaling. This branch has no native batch API: its array-input path
loops over points in C++. Array histogram selection is shared among the declared
components within one point; separate systematic selections are not requested.
Seven raw rounds alternate order and check exact output before/after. All
N=1..16/1000/10000/100000 and S=1/2/16 are retained. Timings run on CPU0 after
all builds finish. No speedup relative to historical studies is claimed.

Provenance identifies the build using source/header/binary SHA256 hashes.
Its source_commit is the base and dirty=true because feature sources had not yet
been committed at build time. Its affinity describes the setup process, while
tools/measure-arrays.sh explicitly pins each timing process to CPU0.

Accepted validation: 146 Python tests passed, ROOT unavailable (one skipped), CVMFS cases excluded. Three native suites passed in Release and Python-free ASan/UBSan/float-cast builds. Ruff passed. Mypy 1.19.1 passes all 13 project source files with Python 3.13; the repository Python-3.9 target is blocked by Python-3.10 syntax in the installed uproot dependency (retained logs). Pydantic was pinned below 2.14 as in the repository type-check setup.

N=100000 synthetic two-bin lookup (microseconds per complete call):

| Scope | S | Mode | Median | Min–max |
|---|---:|---|---:|---:|
| native | 1 | array | 1741.320 | 1685.690–1948.010 |
| native | 1 | scalar_components | 1521.870 | 1492.520–1762.190 |
| native | 2 | array | 1797.960 | 1766.490–1989.050 |
| native | 2 | scalar_components | 3027.900 | 3018.080–3201.360 |
| native | 16 | array | 2985.570 | 2871.230–3091.760 |
| native | 16 | scalar_components | 24381.100 | 24233.000–27289.000 |
| python | 1 | array | 947.683 | 922.542–1270.220 |
| python | 1 | scalar_components | 892.760 | 848.036–1035.992 |
| python | 2 | array | 1223.376 | 1199.315–1299.054 |
| python | 2 | scalar_components | 2249.896 | 1896.627–2928.254 |
| python | 16 | array | 2420.769 | 2327.265–2598.615 |
| python | 16 | scalar_components | 17077.883 | 16940.309–21149.756 |

At N=1/S=1, the native array API can cost more than the scalar API. Read all sizes in the retained raw data; the feature is not a guarantee of faster evaluation.
