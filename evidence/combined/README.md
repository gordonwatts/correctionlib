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
