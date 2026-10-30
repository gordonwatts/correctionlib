#include <correction_plugin.h>
#include <stdexcept>
using namespace correction;
class Probe : public PluginNode {
public:
  double evaluate(const std::vector<Variable::Type>&) const override {
    throw std::runtime_error("scalar probe reached");
  }
  void evaluate_batch(const std::vector<BatchInput>& inputs, size_t n,
                      double* output, size_t) const override {
    for (size_t i = 0; i < n; ++i) output[i] = std::get<double>(inputs[0].at(i)) * 2;
  }
};
extern "C" int correctionlib_plugin_api_version() { return plugin_api_version; }
extern "C" const char* correctionlib_plugin_build_id() { return plugin_build_id().c_str(); }
extern "C" void correctionlib_register_nodes_v4(PluginRegistry& registry) {
  registry.register_node("test.batch", 1, [](const PluginConfig& cfg, const PluginContext& ctx) {
    cfg.require_keys({});
    ctx.input_index("x", Variable::VarType::real);
    if (ctx.child_count()) throw std::runtime_error("no children allowed");
    return std::make_shared<Probe>();
  });
}
