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

using namespace correction;
namespace {
std::string dump(const rapidjson::Value& value) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);
  return {buffer.GetString(), buffer.GetSize()};
}
class OwnedNode : public PluginNode {
public:
  // Library must outlive the node and its shared_ptr deleter.
  std::shared_ptr<void> library;
  std::shared_ptr<const PluginNode> node;
  double evaluate(const std::vector<Variable::Type>& values) const override {
    return node->evaluate(values);
  }
};
class ContentNode : public PluginNode {
public:
  explicit ContentNode(Content content): content_(std::move(content)) {}
  double evaluate(const std::vector<Variable::Type>& values) const override {
    return std::visit([&](const auto& node) -> double {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, double>) return node;
      else return node.evaluate(values);
    }, content_);
  }
private:
  Content content_;
};
}

struct PluginConfig::Impl {
  rapidjson::Document doc;
  explicit Impl(const std::string& text) {
    doc.Parse<rapidjson::kParseFullPrecisionFlag>(text.c_str());
    if (doc.HasParseError() || !doc.IsObject())
      throw std::runtime_error("Plugin config must be a JSON object");
    std::set<std::string> keys;
    for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it)
      if (!keys.insert(std::string(it->name.GetString(), it->name.GetStringLength())).second)
        throw std::runtime_error("Duplicate plugin config key");
  }
  const rapidjson::Value& get(const std::string& key) const {
    auto it = doc.FindMember(key.c_str());
    if (it == doc.MemberEnd()) throw std::runtime_error("Plugin config missing: " + key);
    return it->value;
  }
};
PluginConfig::PluginConfig(const std::string& json): impl_(std::make_shared<Impl>(json)) {}
std::string PluginConfig::json() const { return dump(impl_->doc); }
double PluginConfig::real(const std::string& key) const {
  const auto& value = impl_->get(key);
  if (!value.IsNumber()) throw std::runtime_error("Plugin config expects real: " + key);
  return value.GetDouble();
}
int64_t PluginConfig::integer(const std::string& key) const {
  const auto& value = impl_->get(key);
  if (!value.IsInt64()) throw std::runtime_error("Plugin config expects int64: " + key);
  return value.GetInt64();
}
std::string PluginConfig::string(const std::string& key) const {
  const auto& value = impl_->get(key);
  if (!value.IsString()) throw std::runtime_error("Plugin config expects string: " + key);
  return {value.GetString(), value.GetStringLength()};
}
void PluginConfig::require_keys(std::initializer_list<std::string> keys) const {
  std::set<std::string> expected(keys);
  for (const auto& key : expected) impl_->get(key);
  for (auto it = impl_->doc.MemberBegin(); it != impl_->doc.MemberEnd(); ++it)
    if (!expected.count(it->name.GetString()))
      throw std::runtime_error("Unknown plugin config key: " + std::string(it->name.GetString()));
}
PluginContext::PluginContext(const Correction& correction,
    std::vector<std::shared_ptr<const PluginNode>> children):
  correction_(correction), children_(std::move(children)) {}
size_t PluginContext::input_index(const std::string& name, Variable::VarType type) const {
  auto index = detail::find_input_index(name, correction_.inputs());
  if (correction_.inputs()[index].type() != type)
    throw std::runtime_error("Plugin input type mismatch: " + name);
  return index;
}
size_t PluginContext::child_count() const { return children_.size(); }
std::shared_ptr<const PluginNode> PluginContext::child(size_t index) const {
  return children_.at(index);
}

struct PluginRegistry::Impl {
  struct Entry {
    std::shared_ptr<void> library;
    PluginFactory factory;
  };
  mutable std::mutex mutex;
  std::map<std::pair<std::string, int>, Entry> entries;
  std::map<std::string, std::shared_ptr<void>> libraries;
};
PluginRegistry::PluginRegistry(): impl_(std::make_unique<Impl>()) {}
PluginRegistry::~PluginRegistry() = default;
PluginRegistry::PluginRegistry(const PluginRegistry& other): PluginRegistry() {
  std::lock_guard<std::mutex> guard(other.impl_->mutex);
  impl_->entries = other.impl_->entries;
  impl_->libraries = other.impl_->libraries;
}
PluginRegistry& correction::default_plugin_registry() {
  static PluginRegistry registry;
  return registry;
}
void PluginRegistry::register_node(const std::string& type, int version, PluginFactory factory) {
  if (type.find('.') == std::string::npos || type.find('\0') != std::string::npos || version < 1 || !factory)
    throw std::runtime_error("Plugin type must be namespaced, version positive and factory non-null");
  std::lock_guard<std::mutex> guard(impl_->mutex);
  if (!impl_->entries.emplace(std::make_pair(type, version), Impl::Entry{nullptr, std::move(factory)}).second)
    throw std::runtime_error("Duplicate plugin node: " + type + " version " + std::to_string(version));
}
bool PluginRegistry::contains(const std::string& type, int version) const {
  std::lock_guard<std::mutex> guard(impl_->mutex);
  return impl_->entries.count({type, version});
}
std::shared_ptr<const PluginNode> PluginRegistry::create(const std::string& type, int version,
    const PluginConfig& config, const PluginContext& context) const {
  Impl::Entry entry;
  {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    auto it = impl_->entries.find({type, version});
    if (it == impl_->entries.end())
      throw std::runtime_error("Missing plugin node: " + type + " version " + std::to_string(version));
    entry = it->second;
  }
  auto result = std::make_shared<OwnedNode>();
  result->library = entry.library;
  result->node = entry.factory(config, context); // No registry lock across plugin code.
  if (!result->node) throw std::runtime_error("Plugin factory returned null: " + type);
  return result;
}
void PluginRegistry::load_library(const std::string& path) {
  const auto canonical = std::filesystem::canonical(path).string();
  // Serialize loading; registration goes into a separate registry to avoid deadlock
  // and to make failures atomic. Loading must not recursively load this registry.
  std::lock_guard<std::mutex> guard(impl_->mutex);
  if (impl_->libraries.count(canonical)) return;
#ifdef _WIN32
  auto raw = LoadLibraryW(std::filesystem::path(canonical).wstring().c_str());
  if (!raw) throw std::runtime_error("Cannot load plugin library: " + canonical);
  std::shared_ptr<void> library(raw, [](void* p) { FreeLibrary(static_cast<HMODULE>(p)); });
  auto symbol = [&](const char* name) { return GetProcAddress(raw, name); };
#else
  auto raw = dlopen(canonical.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!raw) throw std::runtime_error("Cannot load plugin library " + canonical + ": " + dlerror());
  std::shared_ptr<void> library(raw, [](void* p) { dlclose(p); });
  auto symbol = [&](const char* name) { return dlsym(raw, name); };
#endif
  auto version = reinterpret_cast<int(*)()>(symbol("correctionlib_plugin_api_version"));
  auto build_id = reinterpret_cast<const char*(*)()>(symbol("correctionlib_plugin_build_id"));
  auto register_nodes = reinterpret_cast<void(*)(PluginRegistry&)>(symbol("correctionlib_register_nodes_v1"));
  if (!version || !register_nodes || version() != plugin_api_version)
    throw std::runtime_error("Incompatible plugin API: " + canonical);
  if (!build_id || !build_id() || plugin_build_id() != build_id())
    throw std::runtime_error("Incompatible plugin build: " + canonical);
  PluginRegistry staging;
  register_nodes(staging);
  if (staging.impl_->entries.empty()) throw std::runtime_error("Plugin library registers no nodes: " + canonical);
  for (const auto& item : staging.impl_->entries)
    if (impl_->entries.count(item.first)) throw std::runtime_error("Duplicate plugin node: " + item.first.first);
  // Prepare the complete replacement before modifying the live registry.
  auto entries = impl_->entries;
  auto libraries = impl_->libraries;
  for (auto& item : staging.impl_->entries) {
    item.second.library = library;
    entries.emplace(item.first, item.second);
  }
  libraries.emplace(canonical, library);
  impl_->entries.swap(entries);
  impl_->libraries.swap(libraries);
}

struct ExtensionNode::State { std::shared_ptr<const PluginNode> node; };
ExtensionNode::ExtensionNode(const JSONObject& json, const Correction& correction) {
  std::string type(json.getRequired<std::string_view>("type"));
  auto version = json.getRequired<int>("version");
  try {
    std::set<std::string> keys;
    for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
      std::string key(it->name.GetString(), it->name.GetStringLength());
      if (!keys.insert(key).second || (key != "nodetype" && key != "type" &&
          key != "version" && key != "config" && key != "children"))
        throw std::runtime_error("Unknown or duplicate extension field: " + key);
    }
    auto config = PluginConfig(dump(json.getRequiredValue("config")));
    std::vector<std::shared_ptr<const PluginNode>> children;
    if (auto items = json.getOptional<rapidjson::Value::ConstArray>("children"))
      for (const auto& item : *items)
        children.push_back(std::make_shared<ContentNode>(detail::parse_plugin_child(item, correction)));
    PluginContext context(correction, std::move(children));
    auto state = std::make_shared<State>();
    state->node = correction.plugin_registry().create(type, version, config, context);
    state_ = std::move(state);
  } catch (const std::exception& error) {
    throw std::runtime_error("Extension " + type + " v" + std::to_string(version) +
        " in correction " + correction.name() + ": " + error.what());
  }
}
double ExtensionNode::evaluate(const std::vector<Variable::Type>& values) const {
  return state_->node->evaluate(values);
}
