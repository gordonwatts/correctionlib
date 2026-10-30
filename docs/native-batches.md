# Native batches (experimental fork API)

`Correction::evaluate_batch(const std::vector<BatchInput>& inputs, size_t n,
double* output, size_t output_size) const` evaluates one scalar correction at n
points. `PluginNode::evaluate_batch` has the same signature and a scalar fallback.
The core and SDK use C++17 without Python. Python evaluate keeps its existing
broadcasting, flattening and numeric conversion, then dispatches one native batch.

A BatchInput owns a scalar Variable::Type (including strings), or borrows a
float64/int64 column using `BatchInput::reals(ptr, n, stride)` or `integers`.
Strides are signed counts of elements, including zero and negative strides; ptr
addresses the first logical element. Each column length must equal n. Strings
are scalar only. Borrowed storage must stay alive and unchanged until return.
The contiguous output has capacity at least n doubles, must not alias any input,
and belongs to the caller. Null output is allowed only for n=0. Empty batches
validate argument count, column lengths and types but invoke no node. Errors
may leave a prefix of output written; discard output after an exception.

Root Binning and MultiBinning dispatch native column loops and perform the same
bin/flow selection as scalar evaluation, without calling Correction::evaluate
per point. Their children use ordinary scalar node evaluation. Root extensions
receive one callback (also through library ownership wrappers); old provider
source needs no new override after rebuilding. Other graphs and compound
corrections use a correct scalar fallback. No search sharing across corrections,
SIMD, float32 API or performance guarantee is implied.

SDK API 2 uses `correctionlib_register_nodes_v2`; API 1 binaries are rejected
before registration. Core/header fingerprints change too: rebuild every provider
with the same core headers, compiler and standard-library ABI.

```cpp
std::vector<double> x{0.25, 1.25}, out(x.size());
corr->evaluate_batch({correction::BatchInput::reals(x.data(), x.size())},
                     x.size(), out.data(), out.size());
```

```python
out = cset["hist"].evaluate(numpy.array([0.25, 1.25]))
```
