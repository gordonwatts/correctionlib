#include "correction_plugin.h"
#include "correction_detail.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <filesystem>
#include <mutex>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Native implementation of the plugin boundary. Configuration and registry
// lookup happen while loading a correction; evaluation calls only the prepared
// node. Python discovery feeds this same registry through the bindings.
using namespace correction;

namespace {
// Copy JSON across the SDK boundary as text instead of exposing RapidJSON
// types or borrowing the correction-set parser's temporary document.
std::string dump(const rapidjson::Value& value) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);
  return {buffer.GetString(), buffer.GetSize()};
}

// Pair a provider's implementation with the library containing its code.
// A correction can outlive both its registry and the CorrectionSet that created it.
class OwnedNode : public PluginNode {
public:
  // Members are destroyed in reverse declaration order. Keep this order: the
  // node destructor AND its shared_ptr deleter must finish before dlclose or
  // FreeLibrary can release the code that implements them.
  std::shared_ptr<void> library;
  std::shared_ptr<const PluginNode> node;

  double evaluate(const std::vector<Variable::Type>& values) const override {
    return node->evaluate(values);
  }
  size_t output_size() const override { return node->output_size(); }
  void evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const override {
    node->evaluate_array(values, output, size);
  }

};

// Let a plugin retain and evaluate an ordinary Content subtree through the
// same interface as a plugin child. The subtree owns its normal native state;
// no reference to the JSON document or construction context escapes here.
class ContentNode : public PluginNode {
public:
  explicit ContentNode(Content content, size_t size) : content_(std::move(content)), size_(size) {}

  double evaluate(const std::vector<Variable::Type>& values) const override {
    return std::visit(
        [&](const auto& node) -> double {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, double>)
            return node;
          else
            return node.evaluate(values);
        },
        content_);
  }

  size_t output_size() const override { return size_; }
  void evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const override {
    if (size != size_) throw std::runtime_error("Child output shape mismatch");
    detail::evaluate_content_array(content_, values, output, size);
  }
private:
  Content content_;
  size_t size_;
};
} // namespace

// Configuration ownership and validation.
// The SDK's pimpl keeps the parser private while allowing configuration handles
// to be copied safely if a provider needs them after construction.
struct PluginConfig::Impl {
  rapidjson::Document doc;

  explicit Impl(const std::string& text) {
    // Match the main payload parser's conversion to double. The default fast
    // parse can round a calibration parameter differently by one ULP.
    doc.Parse<rapidjson::kParseFullPrecisionFlag>(text.c_str());
    if (doc.HasParseError() || !doc.IsObject())
      throw std::runtime_error("Plugin config must be a JSON object");

    // Reject ambiguous input instead of allowing a provider to observe a
    // different duplicate-key value than the host or a Python validator.
    std::set<std::string> keys;
    for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it)
      if (!keys.insert(std::string(it->name.GetString(), it->name.GetStringLength())).second)
        throw std::runtime_error("Duplicate plugin config key");
  }

  const rapidjson::Value& get(const std::string& key) const {
    auto it = doc.FindMember(key.c_str());
    if (it == doc.MemberEnd())
      throw std::runtime_error("Plugin config missing: " + key);
    return it->value;
  }
};

PluginConfig::PluginConfig(const std::string& json) : impl_(std::make_shared<Impl>(json)) {}

std::string PluginConfig::json() const {
  return dump(impl_->doc);
}

double PluginConfig::real(const std::string& key) const {
  const auto& value = impl_->get(key);
  if (!value.IsNumber())
    throw std::runtime_error("Plugin config expects real: " + key);
  return value.GetDouble();
}

int64_t PluginConfig::integer(const std::string& key) const {
  const auto& value = impl_->get(key);
  if (!value.IsInt64())
    throw std::runtime_error("Plugin config expects int64: " + key);
  return value.GetInt64();
}

std::string PluginConfig::string(const std::string& key) const {
  const auto& value = impl_->get(key);
  if (!value.IsString())
    throw std::runtime_error("Plugin config expects string: " + key);
  return {value.GetString(), value.GetStringLength()};
}

// Providers opt into an exact set of required keys: check omissions first,
// then reject typos or fields whose meaning the provider does not implement.
void PluginConfig::require_keys(std::initializer_list<std::string> keys) const {
  std::set<std::string> expected(keys);
  for (const auto& key : expected)
    impl_->get(key);
  for (auto it = impl_->doc.MemberBegin(); it != impl_->doc.MemberEnd(); ++it)
    if (!expected.count(it->name.GetString()))
      throw std::runtime_error("Unknown plugin config key: " + std::string(it->name.GetString()));
}

// Construction services. Resolve names and types once; an evaluator stores
// the returned index and owning child handles, never this borrowed context.
PluginContext::PluginContext(const Correction& correction,
                             std::vector<std::shared_ptr<const PluginNode>> children, size_t output_size)
    : output_size_(output_size), correction_(correction), children_(std::move(children)) {}

size_t PluginContext::input_index(const std::string& name, Variable::VarType type) const {
  auto index = detail::find_input_index(name, correction_.inputs());
  if (correction_.inputs()[index].type() != type)
    throw std::runtime_error("Plugin input type mismatch: " + name);
  return index;
}

size_t PluginContext::child_count() const {
  return children_.size();
}

std::shared_ptr<const PluginNode> PluginContext::child(size_t index) const {
  return children_.at(index);
}

// Registry state and library ownership.
// Each loaded correction set takes a copy under the mutex, so subsequent
// registration cannot change the factories used while that set is built.
struct PluginRegistry::Impl {
  struct Entry {
    // As in OwnedNode, the factory's callable/deleter must be destroyed before
    // its library. A null handle denotes explicitly linked registration.
    std::shared_ptr<void> library;
    PluginFactory factory;
  };

  mutable std::mutex mutex;
  std::map<std::pair<std::string, int>, Entry> entries;
  std::map<std::string, std::shared_ptr<void>> libraries;
};

PluginRegistry::PluginRegistry() : impl_(std::make_unique<Impl>()) {}

PluginRegistry::~PluginRegistry() = default;

// Snapshot the maps, sharing library handles but not mutable registration state.
PluginRegistry::PluginRegistry(const PluginRegistry& other) : PluginRegistry() {
  std::lock_guard<std::mutex> guard(other.impl_->mutex);
  impl_->entries = other.impl_->entries;
  impl_->libraries = other.impl_->libraries;
}

PluginRegistry& correction::default_plugin_registry() {
  static PluginRegistry registry;
  return registry;
}

// Explicit registration and dynamic loading use the same collision policy.
// Namespacing keeps provider IDs distinct from reserved built-in nodetype names.
void PluginRegistry::register_node(const std::string& type, int version, PluginFactory factory) {
  if (type.find('.') == std::string::npos || type.find('\0') != std::string::npos || version < 1 ||
      !factory)
    throw std::runtime_error(
        "Plugin type must be namespaced, version positive and factory non-null");
  std::lock_guard<std::mutex> guard(impl_->mutex);
  if (!impl_->entries
           .emplace(std::make_pair(type, version), Impl::Entry{nullptr, std::move(factory)})
           .second)
    throw std::runtime_error("Duplicate plugin node: " + type + " version " +
                             std::to_string(version));
}

bool PluginRegistry::contains(const std::string& type, int version) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->entries.count({type, version});
}

std::shared_ptr<const PluginNode> PluginRegistry::create(const std::string& type, int version,
                                                         const PluginConfig& config,
                                                         const PluginContext& context) const {
  Impl::Entry entry;
  {
    // Copy the callable and its library owner before releasing the lock.
    std::lock_guard<std::mutex> guard(impl_->mutex);
    auto it = impl_->entries.find({type, version});
    if (it == impl_->entries.end())
      throw std::runtime_error("Missing plugin node: " + type + " version " +
                               std::to_string(version));
    entry = it->second;
  }

  // Invoke foreign code without the registry mutex. Factories may themselves
  // query the registry; holding the lock here would risk a recursive deadlock.
  // The copied entry pins the library even if construction throws.
  auto result = std::make_shared<OwnedNode>();
  result->library = entry.library;
  result->node = entry.factory(config, context);
  if (!result->node)
    throw std::runtime_error("Plugin factory returned null: " + type);
  return result;
}

// A load is one transaction: open -> check ABI -> stage registration -> commit.
// No factory becomes visible unless every step succeeds. The local library
// owner is declared before the staging registry, so staged callables are
// destroyed before an exception path can unload the provider's code.
void PluginRegistry::load_library(const std::string& path) {
  // Normalize explicit paths so repeated loads through relative paths or
  // symlinks do not register the same provider twice.
  const auto canonical = std::filesystem::canonical(path).string();

  // Serialize loading; registration goes into a separate registry to avoid deadlock
  // and to make failures atomic. Loading must not recursively load this registry.
  std::lock_guard<std::mutex> guard(impl_->mutex);
  if (impl_->libraries.count(canonical))
    return;

#ifdef _WIN32
  auto raw = LoadLibraryW(std::filesystem::path(canonical).wstring().c_str());
  if (!raw)
    throw std::runtime_error("Cannot load plugin library: " + canonical);
  std::shared_ptr<void> library(raw, [](void* p) { FreeLibrary(static_cast<HMODULE>(p)); });
  auto symbol = [&](const char* name) { return GetProcAddress(raw, name); };
#else
  auto raw = dlopen(canonical.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!raw)
    throw std::runtime_error("Cannot load plugin library " + canonical + ": " + dlerror());
  std::shared_ptr<void> library(raw, [](void* p) { dlclose(p); });
  auto symbol = [&](const char* name) { return dlsym(raw, name); };
#endif
  // Check exported entry points before calling registration. The build ID is
  // deliberately conservative because this is a C++ SDK, not a stable C ABI.
  auto version = reinterpret_cast<int (*)()>(symbol("correctionlib_plugin_api_version"));
  auto build_id = reinterpret_cast<const char* (*)()>(symbol("correctionlib_plugin_build_id"));
  auto register_nodes =
      reinterpret_cast<void (*)(PluginRegistry&)>(symbol("correctionlib_register_nodes_v3"));
  if (!version || !register_nodes || version() != plugin_api_version)
    throw std::runtime_error("Incompatible plugin API: " + canonical);
  if (!build_id || !build_id() || plugin_build_id() != build_id())
    throw std::runtime_error("Incompatible plugin build: " + canonical);

  // Providers register into an isolated registry. Even a callback that adds
  // some factories and then throws leaves the live registry untouched.
  PluginRegistry staging;
  register_nodes(staging);
  if (staging.impl_->entries.empty())
    throw std::runtime_error("Plugin library registers no nodes: " + canonical);
  for (const auto& item : staging.impl_->entries)
    if (impl_->entries.count(item.first))
      throw std::runtime_error("Duplicate plugin node: " + item.first.first);

  // Check all collisions before committing. Prepare complete replacement maps
  // first so allocation/copy failures also leave the live registry unchanged.
  // Attach the library owner to each factory, not just to the path cache:
  // registry snapshots and surviving nodes need to retain it independently.
  auto entries = impl_->entries;
  auto libraries = impl_->libraries;
  for (auto& item : staging.impl_->entries) {
    item.second.library = library;
    entries.emplace(item.first, item.second);
  }
  libraries.emplace(canonical, library);

  // Both swaps happen under the load lock; readers see the complete old or
  // complete new state, never a partially registered provider.
  impl_->entries.swap(entries);
  impl_->libraries.swap(libraries);
}

// Bridge from the fixed Content variant to a dynamically provided node.
// Only the owning handle survives construction; evaluation performs no parsing,
// discovery, registry lookup or library loading.
struct ExtensionNode::State {
  std::shared_ptr<const PluginNode> node;
};

ExtensionNode::ExtensionNode(const JSONObject& json, const Correction& correction) {
  std::string type(json.getRequired<std::string_view>("type"));
  auto version = json.getRequired<int>("version");
  try {
    // The host validates the envelope; the provider validates its opaque config.
    std::set<std::string> keys;
    for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
      std::string key(it->name.GetString(), it->name.GetStringLength());
      if (!keys.insert(key).second || (key != "nodetype" && key != "type" && key != "version" &&
                                       key != "config" && key != "children" && key != "output_shape"))
        throw std::runtime_error("Unknown or duplicate extension field: " + key);
    }
    auto config = PluginConfig(dump(json.getRequiredValue("config")));

    // Parse children through the host's ordinary recursive parser, preserving
    // built-ins, generic-formula references and nested plugin behavior. A
    // provider sees only owning PluginNode handles, not the Content variant.
    std::vector<std::shared_ptr<const PluginNode>> children;
    if (auto items = json.getOptional<rapidjson::Value::ConstArray>("children"))
      for (const auto& item : *items)
        children.push_back(
            std::make_shared<ContentNode>(detail::parse_plugin_child(item, correction), detail::content_output_size(item)));

    // Context borrows the correction only for the factory call. Providers must
    // retain resolved input indices or child handles instead of that reference.
    size_t output_size = 0;
    if (json.FindMember("output_shape") != json.MemberEnd()) {
      const auto shape = json.getRequired<rapidjson::Value::ConstArray>("output_shape");
      output_size = static_cast<size_t>(shape[0].GetUint64());
    }
    PluginContext context(correction, std::move(children), output_size);
    auto state = std::make_shared<State>();
    state->node = correction.plugin_registry().create(type, version, config, context);
    if (state->node->output_size() != output_size)
      throw std::runtime_error("Plugin output shape disagrees with extension declaration");
    state_ = std::move(state);
  } catch (const std::exception& error) {
    // Add the available correction/type context without hiding the provider's
    // original configuration or construction error.
    throw std::runtime_error("Extension " + type + " v" + std::to_string(version) +
                             " in correction " + correction.name() + ": " + error.what());
  }
}

double ExtensionNode::evaluate(const std::vector<Variable::Type>& values) const {
  return state_->node->evaluate(values);
}

void PluginNode::evaluate_array(const std::vector<Variable::Type>&, double*, size_t) const {
  throw std::runtime_error("Scalar plugin does not implement array outputs");
}
void ExtensionNode::evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const {
  state_->node->evaluate_array(values, output, size);
}
