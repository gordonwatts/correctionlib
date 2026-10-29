#include <correction_plugin.h>
#include <cmath>
#include <stdexcept>
using namespace correction;
namespace {
class Scale : public PluginNode {
  size_t input_;
  double factor_;
public:
  Scale(size_t input, double factor): input_(input), factor_(factor) {}
  double evaluate(const std::vector<Variable::Type>& values) const override {
    return std::get<double>(values.at(input_)) * factor_;
  }
};
class Offset : public PluginNode {
  std::shared_ptr<const PluginNode> child_;
  double amount_;
public:
  Offset(std::shared_ptr<const PluginNode> child, double amount):
    child_(std::move(child)), amount_(amount) {}
  double evaluate(const std::vector<Variable::Type>& values) const override {
    return child_->evaluate(values) + amount_;
  }
};
}
extern "C" int correctionlib_plugin_api_version() { return plugin_api_version; }
extern "C" void correctionlib_register_nodes_v1(PluginRegistry& registry) {
  registry.register_node("org.example.scale", 1, [](const PluginConfig& config, const PluginContext& ctx) {
    config.require_keys({"input", "factor"});
    if (ctx.child_count() != 0) throw std::runtime_error("scale requires no children");
    auto factor = config.real("factor");
    if (!std::isfinite(factor)) throw std::runtime_error("factor must be finite");
    return std::make_shared<Scale>(ctx.input_index(config.string("input"), Variable::VarType::real), factor);
  });
  registry.register_node("org.example.offset", 1, [](const PluginConfig& config, const PluginContext& ctx) {
    config.require_keys({"amount"});
    if (ctx.child_count() != 1) throw std::runtime_error("offset requires one child");
    auto amount = config.real("amount");
    if (!std::isfinite(amount)) throw std::runtime_error("amount must be finite");
    return std::make_shared<Offset>(ctx.child(0), amount);
  });
}

extern "C" const char* correctionlib_plugin_build_id() { return correction::plugin_build_id().c_str(); }
