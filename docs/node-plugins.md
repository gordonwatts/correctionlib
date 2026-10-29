# Native correction node plugins

This fork adds an experimental C++ SDK. Python is optional at build and runtime.
It does not promise a stable cross-compiler C ABI. Rebuild providers against the
installed headers and library when updating the core or toolchain. Existing
native consumers must be rebuilt because the public Content variant changed.

## Build and install without Python

Check out the native submodules (`rapidjson`, `cpp-peglib`, `xxhash`, `pcg-cpp`,
`lwtnn`, `eigen`). The optional `pybind11` submodule is unnecessary for this path.
The bundled lwtnn build downloads Boost headers, so its first build needs network
access. The core needs C++17 and CMake; it does not run a Python packaging backend.

```sh
cmake -S . -B build -DCORRECTIONLIB_BUILD_PYTHON=OFF -DBUILD_DEMO=OFF \
  -DCMAKE_INSTALL_PREFIX=/path/to/install
cmake --build build -j2
cmake --install build
cmake -S examples/node_plugin -B provider -DCMAKE_PREFIX_PATH=/path/to/install \
  -DCMAKE_INSTALL_PREFIX=/path/to/install
cmake --build provider -j2
cmake --install provider
cmake -S examples/node_plugin/consumer -B consumer \
  -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build consumer -j2
./consumer/native_plugin_consumer
```

The core exports its `correctionlib` CMake target and installs
`correction_plugin.h`. The example provider and consumer are independent CMake
projects that use this installation. The same provider can be linked directly
or loaded at runtime; dynamic loading is not required for native applications.

## Native interface

Implement `correction::PluginNode::evaluate(values) const`, returning one double.
Register factories with a namespaced type ID and a positive integer configuration
version. Factories receive `PluginConfig` and a construction-only `PluginContext`.
The latter resolves typed input indices and returns owning handles to parsed
children. Store input indices and child handles, never the context reference.
All runtime state must support concurrent/reentrant calls. Immutable state is
recommended. No Python objects or GIL operations belong in a native node.

`PluginConfig` owns JSON and provides typed real, int64 and string accessors plus
strict key validation. For more elaborate configurations, use `json()` with a
provider-owned native parser. Native factories must validate every required
configuration, child count and input type: Python validators are optional.

A shared provider exports these C-linkage symbols (with symbol visibility enabled;
the example uses CMake `WINDOWS_EXPORT_ALL_SYMBOLS`):

```cpp
extern "C" int correctionlib_plugin_api_version();
extern "C" const char* correctionlib_plugin_build_id();
extern "C" void correctionlib_register_nodes_v1(correction::PluginRegistry&);
```

Their implementations are illustrated in `examples/node_plugin/nodes.cc`. Return
`correction::plugin_api_version` and `correction::plugin_build_id().c_str()` from
the first two. The build handshake compares the core version, public header
fingerprints, compiler version, standard-library ABI marker where available and
input variant size. This rejects common incompatible builds; it cannot certify
arbitrary compiler flags or third-party dependency compatibility.

In C++, create a `PluginRegistry`, call the linked registration function or
`registry.load_library(absolute_path)`, then pass the registry to
`CorrectionSet::from_file(path, registry)` or `from_string(text, registry)`.
The default overloads snapshot `default_plugin_registry()`. Registration rejects
duplicates. Library loading is transactional and idempotent per canonical path.
Payloads never select filesystem library paths or download/install providers.
Load only application-selected native libraries. Registration functions should
only register factories in their supplied registry; they must not recursively
load into the registry currently loading them.

Each correction set snapshots the registry at construction. Registered factories
are copied under a mutex and called outside it. Node instances retain library
ownership, including when a correction outlives its correction set or registry.
The loader and callbacks use the host core instance; do not statically embed a
second CorrectionLib core in the plugin.

## Payload format and compatibility

```json
{
  "nodetype": "extension",
  "type": "org.example.offset",
  "version": 1,
  "config": {"amount": 3.0},
  "children": [
    {
      "nodetype": "extension",
      "type": "org.example.scale",
      "version": 1,
      "config": {"input": "x", "factor": 2.0}
    }
  ]
}
```

`children` defaults to an empty list. Children are parsed by the host, and may
be built-ins or other extensions. Keep subtrees in `children`, not hidden inside
`config`. Extensions can appear anywhere ordinary Content is accepted, including
bin contents, category branches, flow defaults and transforms. They see transformed
input values through the existing evaluation contract.

The document retains schema_version 2 as a fork extension. This is not a claim
that stock schema-v2 validators or evaluators can load it. Provider configuration
versions are independent of package versions and the host plugin API. Deployments
should pin core/provider packages and native dependencies for reproducibility.
Missing types, unsupported versions and invalid configuration fail during load.
Diagnostics include extension type/version and correction name, with JSON paths
added by Python discovery. Native diagnostics do not yet include full subtree paths.

## Python package discovery

Install this core, then build/install `examples/node_plugin` with the installed
core CMake directory supplied as `cmake.define.correctionlib_DIR`. The package's
pyproject.toml declares:

```toml
[project.entry-points."correctionlib.nodes"]
"org.example.scale" = "example_correction_nodes:scale"
"org.example.offset" = "example_correction_nodes:offset"
```

Each callable returns `correctionlib.plugins.NodePlugin` with a library path,
supported configuration versions, and optionally a Pydantic configuration model.
The entry-point name is the namespaced native type. Discovery inspects installed
metadata and imports only required providers; unrelated broken providers do not
prevent ordinary corrections from loading. Duplicate providers are an error.
Registration is idempotent when multiple types share one native library.

High-level loads and pickle restoration discover providers before native parsing.
Worker processes need the provider installed in their own environment. Direct
`_core` users explicitly call `plugins.load_library(path)` before parsing, or use
`plugins.prepare(json_text)` for discovery. `plugins.enable_discovery(False)`
disables automatic imports while preserving explicitly registered native nodes.

Schema/CLI validation checks the extension envelope and recursively validates
children, applies optional provider models, then constructs native evaluators to
check the full native contract. Schema generation itself needs no providers.
The portable envelope schema describes opaque config, not every provider's schema.
Summaries report unknown input usage/ranges for opaque native behavior. NumPy and
Awkward use the existing scalar native evaluation path; this API adds no graph-wide
batching, multiple outputs or Python numerical callbacks.

## Tests

`tests/native_plugins` can build against an installed core; it covers explicit
registration, shared loading, failed registration rollback, duplicate factories,
API/build mismatch, nesting, configuration errors, ownership and concurrency.
`CORRECTIONLIB_BUILD_TESTS=ON` also includes these tests in the core build.
The separate Node plugins workflow builds the core/provider/consumer in a minimal
container with no Python or pybind11, then tests Python discovery with an actual
installed provider wheel on Linux, macOS and Windows. `tests/test_plugins.py`
requires that example wheel for integration tests and skips them when absent.
