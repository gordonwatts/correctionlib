# Combined native batches and fixed array outputs

Develop contains normal merge commits of PR1, PR2 and PR3, followed by
cross-feature API4 wiring. Standalone PRs remain open and independently tested.
All execution uses WSL2 atlas_al9 / AnalysisBase25.2.100 / GCC15.2 / Python3.13.
Builds, venvs and providers are on Linux; the background study is unchanged.

```bash
bash tools/build-combined.sh
bash tools/validate-combined.sh
bash tools/sanitize-combined.sh
bash tools/measure-combined.sh
```

The native benchmark compares one evaluate_batch_array call with an explicit C++
loop of evaluate_array calls on the same correction, writing preallocated N\*S
doubles. The Python benchmark compares one complete high-level vector-input call
with a Python loop of scalar-input array-output calls and result stacking. It
includes broadcast/flatten/conversion/output allocation but no rescaling. The
C++ and Python controls have different costs: their ratios cannot isolate Python
overhead. Histogram and external-provider fixtures are separate. Both use
matched float64 inputs/outputs, seven alternating rounds and exact checks
before/after timing; raw files retain repeats and nanoseconds per call. All
N=1..16/1000/10000/100000, S=1/2/16 are present. Timers use CPU0 after all
builds/check setup finish. These synthetic examples imply no real calibration or
full-analysis validation, explicit SIMD, or promised speedup.

Provenance records complete source/header/provider/Python and installed-core
hashes. source_commit/dirty identify the pre-commit build state; source hashes
identify its exact contents. Setup-process affinity may include more CPUs than
the timers, which are explicitly pinned by tools/measure-combined.sh.

Accepted post-merge validation: 279 Python tests passed; ROOT unavailable (one
skipped), CVMFS cases excluded. Five native suites pass both Release and a
Python-free ASan/UBSan/float-cast build, with independently compiled
scalar-batch and array-batch providers. The default plugin batch-array fallback
also passes. Actual older API2/API3 providers fail clearly before registration.
Standalone C++/Python examples ran and verified expected values.

Raw timing files each contain 1,596 data rows. Summary JSON/CSV retain
median/min/max for every fixture, N, S and mode. Selected N=100000, S=16 rows,
microseconds per complete call:

| Scope  | Fixture   | Mode              |     Median |               Min–max |
| ------ | --------- | ----------------- | ---------: | --------------------: |
| native | histogram | batch_array       |   1891.230 |     1851.750–2027.690 |
| native | histogram | point_loop        |   2908.500 |     2842.440–3153.720 |
| native | plugin    | batch_array       |    521.549 |       498.474–561.607 |
| native | plugin    | point_loop        |   1602.480 |     1584.940–1622.720 |
| python | histogram | batch_array       |   2097.007 |     1924.300–2156.718 |
| python | histogram | python_point_loop | 339669.261 | 335639.426–341324.119 |
| python | plugin    | batch_array       |    956.295 |       859.007–968.105 |
| python | plugin    | python_point_loop | 333037.369 | 332149.111–345492.248 |

Pinned local formatting, spelling and type checks pass. The syntax-upgrade hook
was also run separately with its pinned version3.21.2 under Python3.13, since
the isolated system-Python3.9 hook environment is too old for that tool.
Installing the optional TOML reader in the local spelling-check environment
resolved its initial false positives in encoded notebook output. The original
logs remain retained. Both final PR format gates, ROOT checks, dedicated
feature/provider jobs and completed Linux matrix jobs pass; broader CI runs are
linked in progress.txt.
