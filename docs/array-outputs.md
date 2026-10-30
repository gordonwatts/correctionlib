# Fixed array outputs (experimental schema v3)

This feature returns S explicitly declared numeric components for one input point, in a fixed declared order.
It does not request several systematic selectors or differently named corrections.
Schema v2 remains scalar and unchanged. Schema v3 adds `shape: [S]` to a real
output descriptor. S is a positive integer; omitted shape means scalar. An array
of length one remains an array. This fork accepts both versions; older evaluators
reject v3. This is an experimental fork schema, not an upstream standard.

```json
{"schema_version":3,"corrections":[{"name":"pair","version":1,
"inputs":[{"name":"x","type":"real"}],
"output":{"name":"components","type":"real","shape":[2]},
"data":{"nodetype":"binning","input":"x","edges":[0.0,1.0,2.0],
"content":[{"nodetype":"array","content":[1.0,2.0]},
           {"nodetype":"array","content":[3.0,4.0]}],"flow":"clamp"}}]}
```

The explicit `array` node evaluates S scalar child nodes, in declared order.
Numbers, formulas and scalar plugins can be components. Binning, MultiBinning,
Category and Transform preserve their selected child's output shape. Every bin,
category branch, default and numeric-flow result must have the same shape;
there is no implicit scalar broadcast. A numeric scalar flow value is rejected
for an array histogram; use an array-valued flow node instead. Transform's rule stays scalar; its content
can be an array and sees the rewritten inputs. Ragged/higher-rank arrays,
array arithmetic, array Transform rules and array CompoundCorrection stacks
are rejected while loading. CompoundCorrections may contain only scalar corrections. Scalar formulas/PRNG/LWTNN remain scalar leaves,
but can be explicitly placed in an array component. No labels have reserved meaning.

C++ preserves `double Correction::evaluate(values)` for scalar corrections.
It throws on array corrections. `std::vector<double> evaluate_array(values)`
and `void evaluate_array(values, double* output, size_t output_size)` are explicit
array-only methods. `output_size()` is S for arrays and 0 for scalar corrections.
The contiguous caller-owned buffer needs at least S doubles, must not alias
inputs and may be partially written on an exception. Storage is borrowed only
until return. Returned vectors own their values. Evaluators are reentrant; providers must meet the SDK concurrent-call contract.

```cpp
auto values = cset->at("pair")->evaluate_array({0.25}); // {1.0, 2.0}
```

```python
pair = cset["pair"]
a = pair.evaluate(0.25)                    # shape (2,)
b = pair.evaluate(numpy.array([0.25, 1.25, 0.5])) # shape (3, 2)
```

Python appends (S,) to the broadcast input shape; an empty one-dimensional input produces (0,S);
multidimensional inputs retain their broadcast dimensions before the component axis.
NumPy conversions and scalar argument types retain their existing rules.
Awkward and Dask-Awkward array-output adapters are explicitly unsupported in
this first version; scalar-output behavior is unchanged.

A shaped extension declares `output_shape: [S]` in its envelope, implements
`PluginNode::output_size()` and `evaluate_array(values, output, output_size)`,
and receives its declared size through `PluginContext::output_size()`.
Scalar extensions omit output_shape. The factory and host must agree on size;
children retain independent output sizes through owning handles. Scalar-only
providers are rejected as array providers at construction, never broadcast.
SDK API 3 uses correctionlib_register_nodes_v3 and updated header fingerprints;
all providers must be rebuilt with matching core/toolchain. API 1/2 binaries fail
with an incompatibility error before their registration callback is called.

This PR does not contain the native batch API. Python vector inputs here use
the scalar native loop. The planned integration branch will add batch evaluation of array
outputs with row-major C++ layout `output[row*S + component]` and Python (N,S).
