#include <correction_plugin.h>
#include <stdexcept>
using namespace correction;
namespace {
class Components : public PluginNode {
  size_t input_, size_;
public:
  Components(size_t input, size_t size) : input_(input), size_(size) {}
  size_t output_size() const override { return size_; }
  double evaluate(const std::vector<Variable::Type>&) const override {
    throw std::runtime_error("components is array-valued");
  }
  void evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const override {
    if (!output || size < size_) throw std::invalid_argument("array provider buffer");
    const double x = std::get<double>(values.at(input_));
    for (size_t j = 0; j < size_; ++j) output[j] = x + static_cast<double>(j);
  }
  void evaluate_batch_array(const std::vector<BatchInput>& inputs, size_t n,
                            double* output, size_t) const override {
    for (size_t row = 0; row < n; ++row) {
      const auto x = std::get<double>(inputs.at(input_).at(row));
      for (size_t j = 0; j < size_; ++j) output[row * size_ + j] = x + static_cast<double>(j);
    }
  }

};
// Dispatch probe: scalar-array evaluation is forbidden, so successful vector
// evaluation proves the host called the batch-array callback through OwnedNode.
class BatchOnlyComponents : public Components {
public:
  using Components::Components;
  void evaluate_array(const std::vector<Variable::Type>&, double*, size_t) const override {
    throw std::runtime_error("scalar array probe reached");
  }
};
class Offset : public PluginNode {
  std::shared_ptr<const PluginNode> child_;
public:
  explicit Offset(std::shared_ptr<const PluginNode> child) : child_(std::move(child)) {}
  size_t output_size() const override { return child_->output_size(); }
  double evaluate(const std::vector<Variable::Type>&) const override {
    throw std::runtime_error("array offset requires evaluate_array");
  }
  void evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const override {
    child_->evaluate_array(values, output, size);
    for (size_t j = 0; j < output_size(); ++j) output[j] += 10.;
  }
};
}
extern "C" int correctionlib_plugin_api_version() { return plugin_api_version; }
extern "C" const char* correctionlib_plugin_build_id() { return plugin_build_id().c_str(); }
extern "C" void correctionlib_register_nodes_v4(PluginRegistry& registry) {
  registry.register_node("org.example.components", 1, [](const PluginConfig& cfg, const PluginContext& ctx) {
    cfg.require_keys({"input", "size"});
    const auto size = cfg.integer("size");
    if (size < 1 || static_cast<size_t>(size) != ctx.output_size() || ctx.child_count())
      throw std::runtime_error("components size must match declared output_shape; no children");
    return std::make_shared<Components>(ctx.input_index(cfg.string("input"), Variable::VarType::real), size);
  });
  registry.register_node("test.array_batch", 1, [](const PluginConfig& cfg, const PluginContext& ctx) {
    cfg.require_keys({"input", "size"});
    const auto size = cfg.integer("size");
    if (size < 1 || static_cast<size_t>(size) != ctx.output_size() || ctx.child_count())
      throw std::runtime_error("array batch probe size mismatch");
    return std::make_shared<BatchOnlyComponents>(ctx.input_index(cfg.string("input"), Variable::VarType::real), size);
  });
  registry.register_node("org.example.array_offset", 1, [](const PluginConfig& cfg, const PluginContext& ctx) {
    cfg.require_keys({});
    if (ctx.child_count() != 1 || !ctx.output_size() || ctx.child(0)->output_size() != ctx.output_size())
      throw std::runtime_error("array offset needs one child of the declared shape");
    return std::make_shared<Offset>(ctx.child(0));
  });
}
