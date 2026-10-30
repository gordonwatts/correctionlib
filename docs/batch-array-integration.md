# Native batches and fixed array outputs together

A scalar correction evaluated at N input points returns N numbers. A fixed array
correction returns S ordered components for one point. Combining them returns N
rows of S numbers. Components belong to one declared correction; they do not
request several systematic labels or differently named corrections.

The [runnable C++ and Python examples](../examples/batch_arrays) use the same
payload for scalar batching, a single array output, and combined batching.

```cpp
std::vector<double> x{0.25, 1.25, 0.5}, output(x.size() * 2);
std::vector<correction::BatchInput> inputs{
    correction::BatchInput::reals(x.data(), x.size())};
cset->at("pair")->evaluate_batch_array(inputs, x.size(), output.data(), output.size());
// Row-major: output[row * 2 + component].
```

```python
x = numpy.array([0.25, 1.25, 0.5])
cset["scalar"].evaluate(x)  # shape (3,)
cset["pair"].evaluate(0.25)  # shape (2,)
cset["pair"].evaluate(x)  # shape (3, 2)
```

`Correction::evaluate_batch_array(const std::vector<BatchInput>&, size_t n, double* output, size_t output_size) const`
requires an array correction. Capacity is measured in doubles and must be at
least n\*S; larger buffers do not change the logical row stride. Integer
overflow, mismatched columns/types and null nonempty buffers are rejected. Empty
batches validate shape/types/lengths and invoke no node; null output is allowed.
Inputs and output must not alias. Borrowed column lifetimes, signed element
strides and scalar broadcasting follow the [batch contract](native-batches.md).
Exceptions may leave partial output. Python appends the declared component
dimension to the broadcast input shape.

Root Binning/MultiBinning nodes receive one column dispatch; each point selects
one bin whose declared S components are then evaluated. Root extensions receive
`PluginNode::evaluate_batch_array`, with a default per-point evaluate_array
fallback. Other graph paths use shape-correct per-point fallback. A separately
compiled probe throws on evaluate_array yet succeeds on batch-array dispatch.
This verifies the provider callback through the host's library-ownership
wrapper.

Develop uses experimental SDK API4 and correctionlib_register_nodes_v4. The two
standalone PRs use API2 (batch) and API3 (array). Rebuild every provider against
the chosen core's matching headers, compiler and build fingerprint. Older
provider binaries are rejected before registration. The core/SDK/provider remain
C++17 and Python-independent. There is no float32 API or explicit SIMD. The
implementation creates no internal worker threads and does not share searches
across independently selected corrections.

Schema-v2 scalar payloads remain supported; schema-v3 is required for array
nodes and shapes. Array CompoundCorrections, ragged/higher-rank outputs and
Awkward/Dask-Awkward array-output adapters remain unsupported. Scalar adapters
continue to work. [Combined evidence](../evidence/combined) retains validation,
seven timing rounds, machine/compiler metadata and exact build hashes.

Source links: [core dispatch](../src/correction.cc),
[SDK and owning plugin adapters](../src/plugin.cc),
[Python vector dispatch](../src/python.cc), and
[schema-v3 authoring/validation](../src/correctionlib/schemav3.py).
