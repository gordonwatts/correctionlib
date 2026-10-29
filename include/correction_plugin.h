#ifndef CORRECTIONLIB_PLUGIN_H
#define CORRECTIONLIB_PLUGIN_H
#include "correction.h"
#include <functional>
#include <initializer_list>

namespace correction {
// Experimental C++ SDK: matching compiler, standard library and core build required.
constexpr int plugin_api_version = 1;
// Conservative handshake for this experimental C++ ABI. Rebuild providers when
// the core version or compiler changes; this is not a portable stable C ABI.
static inline const std::string& plugin_build_id() {
  static const std::string id = std::string(correctionlib_version) + ":" + std::string(correctionlib_plugin_build_id) + ":" +
#ifdef _MSC_VER
    std::to_string(_MSC_VER) + ":" +
#else
    std::string(__VERSION__) + ":" +
#endif
#ifdef _GLIBCXX_USE_CXX11_ABI
    std::to_string(_GLIBCXX_USE_CXX11_ABI) + ":" +
#endif
    std::to_string(sizeof(Variable::Type));
  return id;
}

class PluginNode {
public:
  virtual ~PluginNode() = default;
  virtual double evaluate(const std::vector<Variable::Type>& values) const = 0;
};

// Owns its JSON; no RapidJSON or Python types cross the SDK boundary.
class PluginConfig {
public:
  explicit PluginConfig(const std::string& json);
  std::string json() const;
  double real(const std::string& key) const;
  int64_t integer(const std::string& key) const;
  std::string string(const std::string& key) const;
  void require_keys(std::initializer_list<std::string> keys) const;
private:
  struct Impl;
  std::shared_ptr<const Impl> impl_;
};

class PluginContext {
public:
  PluginContext(const Correction& correction,
                std::vector<std::shared_ptr<const PluginNode>> children);
  size_t input_index(const std::string& name, Variable::VarType type) const;
  size_t child_count() const;
  std::shared_ptr<const PluginNode> child(size_t index) const;
private:
  const Correction& correction_; // Construction only: factories must not retain this context.
  std::vector<std::shared_ptr<const PluginNode>> children_;
};

using PluginFactory = std::function<std::shared_ptr<const PluginNode>(
    const PluginConfig&, const PluginContext&)>;

class PluginRegistry {
public:
  PluginRegistry();
  PluginRegistry(const PluginRegistry&); // Independent, thread-safe snapshot.
  PluginRegistry& operator=(const PluginRegistry&) = delete;
  ~PluginRegistry();
  void register_node(const std::string& type, int version, PluginFactory factory);
  // Transactional and idempotent per canonical path; never searches payload paths.
  void load_library(const std::string& path);
  bool contains(const std::string& type, int version) const;
  std::shared_ptr<const PluginNode> create(const std::string& type, int version,
      const PluginConfig&, const PluginContext&) const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace correction

// A library exports both functions (use __declspec(dllexport) on Windows):
// extern "C" int correctionlib_plugin_api_version();
// extern "C" const char* correctionlib_plugin_build_id();
// extern "C" void correctionlib_register_nodes_v1(correction::PluginRegistry&);
#endif
