# Native batches (experimental fork API)

`Correction::evaluate_batch(const std::vector<BatchInput>& inputs, size_t n, double* output, size_t output_size) const`
evaluates one scalar correction at n points. `PluginNode::evaluate_batch` has
the same signature and a scalar fallback. The core and SDK use C++17 without
Python. Python evaluate keeps its existing broadcasting, flattening and numeric
conversion, then dispatches one native batch.

A BatchInput owns a scalar Variable::Type (including strings), or borrows a
float64/int64 column using `BatchInput::reals(ptr, n, stride)` or `integers`.
Strides are signed counts of elements, including zero and negative strides; ptr
addresses the first logical element. Each column length must equal n. Strings
are scalar only. Borrowed storage must stay alive and unchanged until return.
The contiguous output has capacity at least n doubles, must not alias any input,
and belongs to the caller. Null output is allowed only for n=0. Empty batches
validate argument count, column lengths and types but invoke no node. Errors may
leave a prefix of output written; discard output after an exception.

At the correction's top-level data node (the root), Binning and MultiBinning
dispatch native column loops and perform the same bin/flow selection as scalar
evaluation, without calling Correction::evaluate per point. Their children use
ordinary scalar node evaluation. Root extensions receive one callback for each
nonempty batch (also through library ownership wrappers); old provider source
needs no new override after rebuilding. Other graphs and compound corrections
use a correct scalar fallback. No search sharing across corrections, SIMD,
float32 API or performance guarantee is implied.

Uniform-bin NaN and nonfinite wrapped coordinates are rejected explicitly,
avoiding a previously undefined floating-to-integer conversion. Malformed
histogram geometry is rejected before evaluation.

SDK API 4 uses `correctionlib_register_nodes_v4`; API 1/2/3 binaries are
rejected before registration. Core/header fingerprints change too: rebuild every
provider with the same core headers, compiler and standard-library ABI.

## Calling from Python and C++

This example keeps a scalar output: each input point produces one number. Use it
with the [complete example payload](../examples/native_batch/payload.json),
running from the repository root.

Python keeps the existing calling convention. Pass a NumPy array to the same
`evaluate(...)` method:

```python
import correctionlib
import numpy as np

cset = correctionlib.CorrectionSet.from_file("examples/native_batch/payload.json")
corr = cset["scalar"]

one = corr.evaluate(0.25)  # 1.0 — one input point
many = corr.evaluate(np.array([0.25, 1.25, 0.5]))
# array([1., 3., 1.]) — one result for each of three points
```

C++ keeps the existing scalar call and adds an explicit batch method. The caller
provides input columns and an output buffer:

```cpp
auto cset = correction::CorrectionSet::from_file("examples/native_batch/payload.json");
auto corr = cset->at("scalar");

double one = corr->evaluate({0.25}); // 1.0, as before

std::vector<double> x{0.25, 1.25, 0.5}, out(x.size());
corr->evaluate_batch({correction::BatchInput::reals(x.data(), x.size())},
                     x.size(), out.data(), out.size());
// out is {1.0, 3.0, 1.0}
```

`BatchInput::reals` borrows the coordinates; it does not copy them. A scalar
`BatchInput` can be reused for every point, for example when a category selector
is the same for the whole batch. Passing array inputs does not require changing
the correction payload or declaring an array-valued output.

## Writing a node: keep scalar, optionally add batching

Keep the existing per-point `evaluate(...)` implementation. If that is all a
node provides, the SDK's default batch method builds each input row and calls
the scalar method. Existing scalar node logic therefore still works after the
provider is updated and rebuilt for the matching SDK.

To process a whole input column directly, add an optional `evaluate_batch(...)`
override. For example, this node multiplies one real input by a fixed factor;
the scalar method is unchanged, and the new method fills the caller's output
buffer:

```cpp
#include <correction_plugin.h>

class Scale : public correction::PluginNode {
public:
  Scale(size_t input, double factor) : input_(input), factor_(factor) {}

  // Existing scalar implementation: one point in, one double out.
  double evaluate(
      const std::vector<correction::Variable::Type>& values) const override {
    return std::get<double>(values.at(input_)) * factor_;
  }

  // Optional batch implementation: n points in, n doubles written to output.
  void evaluate_batch(const std::vector<correction::BatchInput>& inputs,
                      size_t n, double* output,
                      size_t /*output_size*/) const override {
    const auto& column = inputs.at(input_);
    for (size_t row = 0; row < n; ++row) {
      output[row] = std::get<double>(column.at(row)) * factor_;
    }
  }

private:
  size_t input_;
  double factor_;
};
```

The factory still resolves `input_` and validates `factor_` when loading the
node; see the [complete provider](../examples/node_plugin/nodes.cc). Before
calling the batch override, the core validates input types, column lengths and
output capacity. `column.at(row)` handles both borrowed columns and scalar
broadcasting, including the declared stride. Write one result to `output[row]`;
the node does not allocate or return a vector. Inputs and output are borrowed
only for the call, and node state must support concurrent callers as before.

`BatchInput::at(row)` is the supported public reader and returns a
`Variable::Type`. The SDK does not expose typed raw column pointers to
providers. A callback can resolve input columns and select its axes before the
row loop, as the example does for `column`.

A top-level extension receives this batch callback once for a nonempty batch. An
enclosing graph can still use scalar fallback; adding the override does not make
every nested graph a batch kernel. No Python code is needed in the node. The
versioned registration entry point and build fingerprint must match the chosen
SDK, even when keeping the default scalar fallback.

For a correction that also returns several components per point, the separate
`evaluate_batch_array(...)` callback writes rows of components; see the
[combined API](https://github.com/gordonwatts/correctionlib/blob/develop/docs/batch-array-integration.md).
