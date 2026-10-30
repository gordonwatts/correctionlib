#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
#include <rapidjson/document.h>
#pragma GCC diagnostic pop
#include <rapidjson/filereadstream.h>
#include <rapidjson/error/en.h>
#include <optional>
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <cmath>
#include <cstdlib> // std::abort
#include <random>
#include "correction.h"
#include "correction_plugin.h"
#define XXH_INLINE_ALL 1
#include "xxhash.h"
#include "pcg_random.hpp"
#if __has_include(<zlib.h>)
#include <zlib.h>
#include "gzfilereadstream.h"
#define WITH_ZLIB 1
#endif
#include "correction_detail.h"

using namespace correction;

//! helper function for parsing flow behavior from string
detail::FlowBehavior parse_flow_behavior(const rapidjson::Value& flowbehavior) {
  if ( flowbehavior == "clamp" ) {
    return detail::FlowBehavior::clamp;
  }
  else if ( flowbehavior == "error" ) {
    return detail::FlowBehavior::error;
  }
  else if ( flowbehavior == "wrap" ) {
    return detail::FlowBehavior::wrap;
  }
  else {
    return detail::FlowBehavior::value;
  }
}

namespace {
  Content resolve_content(const rapidjson::Value& json, const Correction& context) {
    if ( json.IsNumber() ) { return json.GetDouble(); }
    else if ( json.IsObject() && json.HasMember("nodetype") ) {
      auto obj = JSONObject(json.GetObject());
      auto type = obj.getRequired<std::string_view>("nodetype");
      // Extension is the single dynamic alternative; built-in node dispatch
      // remains unchanged and nested nodes still enter through this parser.
      if ( type == "array" ) { return Array(obj, context); }
      else if ( type == "extension" ) { return ExtensionNode(obj, context); }
      else if ( type == "binning" ) { return Binning(obj, context); }
      else if ( type == "multibinning" ) { return MultiBinning(obj, context); }
      else if ( type == "category" ) { return Category(obj, context); }
      else if ( type == "formula" ) { return Formula(obj, context); }
      else if ( type == "formularef" ) { return FormulaRef(obj, context); }
      else if ( type == "transform" ) { return Transform(obj, context); }
      else if ( type == "hashprng" ) { return HashPRNG(obj, context); }
      else if ( type == "lwtnn") { return LWTNN(obj, context); }
      else { throw std::runtime_error("Unrecognized Content object nodetype"); }
    }
    throw std::runtime_error("Invalid Content node type");
  }

  struct node_evaluate {
    double operator() (double node) { return node; }

    template <class Node>
    double operator() (const Node &node) {
      return node.evaluate(values);
    }

    const std::vector<Variable::Type>& values;
  };

  // Per-thread scratch storage for Transform::evaluate.
  // Depth indexing keeps nested Transform evaluations re-entrant-safe.
  class TransformScratch {
  public:
    explicit TransformScratch(const std::vector<Variable::Type>& values):
      slot_(acquire_slot())
    {
      slot_ = values;
    }

    ~TransformScratch() {
      release_slot();
    }

    std::vector<Variable::Type>& values() {
      return slot_;
    }

  private:
    static std::vector<Variable::Type>& acquire_slot() {
      if (depth_ == slots_.size()) {
        slots_.emplace_back();
      }
      return slots_[depth_++];
    }

    static void release_slot() {
      depth_--;
    }

    std::vector<Variable::Type>& slot_;
    static thread_local std::deque<std::vector<Variable::Type>> slots_;
    static thread_local std::size_t depth_;
  };

  thread_local std::deque<std::vector<Variable::Type>> TransformScratch::slots_;
  thread_local std::size_t TransformScratch::depth_ = 0;

  std::size_t find_bin_idx(Variable::Type value_variant,
                           const detail::EdgesType &bins_,
                           const detail::FlowBehavior &flow,
                           std::size_t variableIdx,
                           const char *name)
  {
    double value = std::visit([](auto&& arg) -> double {
      using T = std::decay_t<decltype(arg)>;
      if constexpr (std::is_same_v<T, int64_t>) return static_cast<double>(arg);
      else if constexpr (std::is_same_v<T, double>) return arg;
      else throw std::logic_error("I should not have ever seen a string");
      }, value_variant);
    if (flow == detail::FlowBehavior::wrap && !std::isfinite(value))
      throw std::runtime_error("Non-finite input cannot be wrapped");
    if ( auto *bins = std::get_if<detail::UniformBins>(&bins_) ) { // uniform binning
      // NaN otherwise reaches a floating-to-integer cast with undefined behavior.
      if (std::isnan(value)) throw std::runtime_error("NaN input in uniform binning");
      if (value < bins->low || value >= bins->high) {
        switch (flow) {
          case detail::FlowBehavior::value:
            return bins->n; // the default value is stored at the end of the content array, after the last bin
          case detail::FlowBehavior::clamp:
            return value < bins->low ? 0 : bins->n - 1; // assuming we always have at least 1 bin
          case detail::FlowBehavior::wrap:
            break;
          case detail::FlowBehavior::error:
            const std::string belowOrAbove = value < bins->low ? "below" : "above";
            auto msg = "Index " + belowOrAbove + " bounds in " + name + " for input argument " + std::to_string(variableIdx) + " value: " + std::to_string(value);
            throw std::runtime_error(std::move(msg));
        }
      }

      double norm_value = ((value - bins->low) / (bins->high - bins->low));
      if (flow == detail::FlowBehavior::wrap) {
        norm_value -= std::floor(norm_value);
      }
      std::size_t binIdx = bins->n * norm_value;
      return binIdx;
    }

    // otherwise we have non-uniform binning
    using namespace std::string_literals;
    const auto& bins = std::get<detail::NonUniformBins>(bins_);
    if ( flow == detail::FlowBehavior::wrap ) {
      double low = bins[0];
      double high = bins[bins.size() - 1];
      double norm_value = (value - low) / (high - low);
      norm_value -= std::floor(norm_value);
      value = low + norm_value * (high - low);
    }

    auto it = std::upper_bound(std::begin(bins), std::end(bins), value);
    if ( it == std::begin(bins) ) { // underflow
      if ( flow == detail::FlowBehavior::value ) {
        return bins.size() - 1; // the default value is stored at the end of the content array, after the last bin
      }
      else if ( flow == detail::FlowBehavior::error ) {
        throw std::runtime_error("Index below bounds in "s + name + " for input argument " + std::to_string(variableIdx) + " value: " + std::to_string(value));
      }
      else if ( flow == detail::FlowBehavior::wrap ) {
        throw std::logic_error("I should not have ever seen an underflow");
      }
      else { // clamp
        it++;
      }
    }
    else if ( it == std::end(bins) ) { // overflow
      if ( flow == detail::FlowBehavior::value ) {
        return bins.size() - 1;
      }
      else if ( flow == detail::FlowBehavior::error ) {
        throw std::runtime_error("Index above bounds in "s + name + " for input argument " + std::to_string(variableIdx) + " value: " + std::to_string(value));
      }
      else if ( flow == detail::FlowBehavior::wrap ) {
        throw std::logic_error("I should not have ever seen an overflow");
      }
      else { // clamp
        it--;
      }
    }

    // -1 because upper_bound returns the edge _after_ the bin we are interested in
    const std::size_t binIdx = std::distance(std::begin(bins), it) - 1;
    return binIdx;
  }

  double parse_edge(const rapidjson::Value& edge) {
    if ( edge.IsDouble() ) {
      return edge.GetDouble();
    } else if ( edge.IsString() ) {
      std::string_view str = edge.GetString();
      if ((str == "inf") || (str == "+inf")) return std::numeric_limits<double>::infinity();
      else if (str == "-inf") return -std::numeric_limits<double>::infinity();
    }
    throw std::runtime_error("Invalid edge type");
  }

  std::vector<double> parse_bin_edges(const rapidjson::Value::ConstArray& edges) {
    if (edges.Size() < 2) throw std::runtime_error("Binning needs at least two edges");
    std::vector<double> result;
    result.reserve(edges.Size());
    for (const auto& edge : edges) {
      double val = parse_edge(edge);
      if (std::isnan(val)) throw std::runtime_error("NaN bin edge");
      if ( result.size() > 0 && result.back() >= val ) {
        throw std::runtime_error("binning edges are not monotone increasing");
      }
      result.push_back(val);
    }
    return result;
  }
} // end of anonymous namespace

Variable::Variable(const JSONObject& json) :
  name_(json.getRequired<const char *>("name")),
  description_(json.getOptional<const char*>("description").value_or(""))
{
  auto type = json.getRequired<std::string_view>("type");
  if (type == "string") { type_ = VarType::string; }
  else if (type == "int") { type_ = VarType::integer; }
  else if (type == "real") { type_ = VarType::real; }
  else { throw std::runtime_error("Unrecognized variable type"); }
}

std::string Variable::typeStr() const {
  if ( type_ == VarType::string ) { return "string"; }
  else if ( type_ == VarType::integer ) { return "int"; }
  else if ( type_ == VarType::real ) { return "real"; }
  return "";
}

void Variable::validate(const Type& t) const {
  if ( std::holds_alternative<std::string>(t) ) {
    if ( type_ != VarType::string ) {
      throw std::runtime_error("Input " + name() + " has wrong type: got string expected " + typeStr());
    }
  }
  else if ( std::holds_alternative<int64_t>(t) ) {
    if ( type_ != VarType::integer ) {
      throw std::runtime_error("Input " + name() + " has wrong type: got int expected " + typeStr());
    }
  }
  else if ( std::holds_alternative<double>(t) ) {
    if ( type_ != VarType::real ) {
      throw std::runtime_error("Input " + name() + " has wrong type: got real-valued expected " + typeStr());
    }
  }
}

Variable Variable::from_string(const char * data) {
  rapidjson::Document json;
  rapidjson::ParseResult ok = json.Parse(data);
  if (!ok) {
    throw std::runtime_error(
        std::string("JSON parse error: ") + rapidjson::GetParseError_En(ok.Code())
        + " at offset " + std::to_string(ok.Offset())
        );
  }
  if ( ! json.IsObject() ) { throw std::runtime_error("Expected Variable object"); }
  return Variable(json);
}

Formula::Formula(const JSONObject& json, const Correction& context, bool generic)
  : Formula(json, context.inputs(), generic) {}

Formula::Formula(const JSONObject& json, const std::vector<Variable>& inputs, bool generic) :
  expression_(json.getRequired<const char *>("expression")),
  generic_(generic)
{
  auto parser_type = json.getRequired<std::string_view>("parser");
  if (parser_type == "TFormula") { type_ = FormulaAst::ParserType::TFormula; }
  else if (parser_type == "numexpr") {
    type_ = FormulaAst::ParserType::numexpr;
    throw std::runtime_error("numexpr formula parser is not yet supported");
  }
  else { throw std::runtime_error("Unrecognized formula parser type"); }

  std::vector<size_t> variableIdx;
  for (const auto& item : json.getRequired<rapidjson::Value::ConstArray>("variables")) {
    auto idx = detail::find_input_index(item.GetString(), inputs);
    if ( inputs[idx].type() != Variable::VarType::real ) {
      throw std::runtime_error("Formulas only accept real-valued inputs, got type "
          + inputs[idx].typeStr() + " for variable " + inputs[idx].name());
    }
    variableIdx.push_back(idx);
  }

  std::vector<double> params;
  if ( auto items = json.getOptional<rapidjson::Value::ConstArray>("parameters") ) {
    for (const auto& item : *items) {
      params.push_back(item.GetDouble());
    }
  }

  ast_ = std::make_unique<FormulaAst>(FormulaAst::parse(type_, expression_, params, variableIdx, !generic));
}

Formula::Ref Formula::from_string(const char * data, std::vector<Variable>& inputs) {
  rapidjson::Document json;
  rapidjson::ParseResult ok = json.Parse(data);
  if (!ok) {
    throw std::runtime_error(
        std::string("JSON parse error: ") + rapidjson::GetParseError_En(ok.Code())
        + " at offset " + std::to_string(ok.Offset())
        );
  }
  if ( ! json.IsObject() ) { throw std::runtime_error("Expected Formula object"); }
  return std::make_shared<Formula>(json, inputs);
}

double Formula::evaluate(const std::vector<Variable::Type>& values) const {
  if ( generic_ ) {
    throw std::runtime_error("Generic formulas must be evaluated with parameters");
  }
  return ast_->evaluate(values, {});
}

double Formula::evaluate(const std::vector<Variable::Type>& values, const std::vector<double>& params) const {
  return ast_->evaluate(values, params);
}

FormulaRef::FormulaRef(const JSONObject& json, const Correction& context) {
  formula_ = context.formula_ref(json.getRequired<int>("index"));
  for (const auto& item : json.getRequired<rapidjson::Value::ConstArray>("parameters")) {
    parameters_.push_back(item.GetDouble());
  }
}

double FormulaRef::evaluate(const std::vector<Variable::Type>& values) const {
  return formula_->evaluate(values, parameters_);
}

Transform::Transform(const JSONObject& json, const Correction& context) {
  variableIdx_ = detail::find_input_index(json.getRequired<std::string_view>("input"), context.inputs());
  const auto& variable = context.inputs()[variableIdx_];
  if ( variable.type() == Variable::VarType::string ) {
    throw std::runtime_error("Transform cannot rewrite string inputs");
  }
  rule_ = std::make_unique<Content>(resolve_content(json.getRequiredValue("rule"), context));
  content_ = std::make_unique<Content>(resolve_content(json.getRequiredValue("content"), context));
}

double Transform::evaluate(const std::vector<Variable::Type>& values) const {
  TransformScratch scratch(values);
  auto& new_values = scratch.values();

  double vnew = std::visit(node_evaluate{values}, *rule_);
  auto& v = new_values[variableIdx_];
  if ( std::holds_alternative<double>(v) ) {
    v = vnew;
  }
  else if ( std::holds_alternative<int64_t>(v) ) {
    v = (int64_t) std::round(vnew);
  }
  else {
    throw std::logic_error("I should not have ever seen a string");
  }
  return std::visit(node_evaluate{new_values}, *content_);
}

void Transform::evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const {
  TransformScratch scratch(values);
  auto& new_values = scratch.values();

  double vnew = std::visit(node_evaluate{values}, *rule_);
  auto& v = new_values[variableIdx_];
  if ( std::holds_alternative<double>(v) ) {
    v = vnew;
  }
  else if ( std::holds_alternative<int64_t>(v) ) {
    v = (int64_t) std::round(vnew);
  }
  else {
    throw std::logic_error("I should not have ever seen a string");
  }
  detail::evaluate_content_array(*content_, new_values, output, size);
}

HashPRNG::HashPRNG(const JSONObject& json, const Correction& context)
{
  const auto& inputs = json.getRequired<rapidjson::Value::ConstArray>("inputs");
  variablesIdx_.reserve(inputs.Size());
  for (const auto& input : inputs) {
    if ( ! input.IsString() ) { throw std::runtime_error("invalid hashprng input type"); }
    size_t idx = detail::find_input_index(input.GetString(), context.inputs());
    if ( context.inputs().at(idx).type() == Variable::VarType::string ) {
      throw std::runtime_error("HashPRNG cannot use string inputs as entropy sources");
    }
    variablesIdx_.push_back(idx);
  }
  auto dist = json.getRequired<std::string_view>("distribution");
  if (dist == "stdflat") { dist_ = Distribution::stdflat; }
  else if (dist == "stdnormal") { dist_ = Distribution::stdnormal; }
  else if (dist == "normal") { dist_ = Distribution::normal; }
  else { throw std::runtime_error("invalid distribution type for hashprng"); }

}

double HashPRNG::evaluate(const std::vector<Variable::Type>& values) const {
  pcg32_oneseq gen;
  size_t nbytes = sizeof(uint64_t)*variablesIdx_.size();
  uint64_t* seedData = (uint64_t*) alloca(nbytes);
  for(size_t i=0; i<variablesIdx_.size(); ++i) {
    if ( auto v = std::get_if<int64_t>(&values[variablesIdx_[i]]) ) {
      seedData[i] = static_cast<uint64_t>(*v);
    }
    else if ( auto v = std::get_if<double>(&values[variablesIdx_[i]]) ) {
      seedData[i] = *reinterpret_cast<const uint64_t*>(v);
    }
    else { throw std::logic_error("I should not have ever seen a string"); }
  }
  gen.seed(XXH64((const void*) seedData, nbytes, 0ul));
  switch (dist_) {
    case Distribution::stdflat:
      return std::uniform_real_distribution<>()(gen);
    case Distribution::stdnormal:
      // if not a temporary, it may reuse the spare value
      return std::normal_distribution<>()(gen);
    case Distribution::normal:
      double u, v, s;
      do {
        // cheap but wrong-ish https://www.pcg-random.org/using-pcg-c.html#generating-doubles
        u = std::ldexp(gen(), -31) - 1;
        v = std::ldexp(gen(), -31) - 1;
        s = u*u + v*v;
      } while ( s>= 1.0 || s == 0.0 );
      return u * std::sqrt(-2.0 * std::log(s) / s);
    default:
      std::abort(); // never reached if the switch is exhaustive
  };
}

Binning::Binning(const JSONObject& json, const Correction& context)
{
  const auto& content = json.getRequired<rapidjson::Value::ConstArray>("content");

  // set bins_
  const auto &edgesObj = json.getRequiredValue("edges");
  if ( edgesObj.IsArray() ) { // non-uniform binning
    std::vector<double> edges = parse_bin_edges(edgesObj.GetArray());
    if ( edges.size() != content.Size() + 1 ) {
      throw std::runtime_error("Inconsistency in Binning: number of content nodes does not match binning");
    }
    detail::NonUniformBins bins{std::move(edges)};
    bins_ = std::move(bins);
  } else if ( edgesObj.IsObject() ) { // UniformBinning
    const JSONObject uniformBins{edgesObj.GetObject()};
    const auto n = uniformBins.getRequired<uint32_t>("n");
    if ( n == 0 ) {
      throw std::runtime_error("Error when processing Binning with UniformBinning: number of bins is zero");
    }
    if ( n != content.Size() ) {
      throw std::runtime_error("Inconsistency in Binning: number of content nodes does not match binning");
    }
    const auto low = uniformBins.getRequired<double>("low");
    const auto high = uniformBins.getRequired<double>("high");
    if (!std::isfinite(low) || !std::isfinite(high) || !(low < high) || !std::isfinite(high-low))
      throw std::runtime_error("Uniform binning requires finite increasing bounds");
    bins_ = detail::UniformBins{n, low, high};
  } else {
    throw std::runtime_error ("Error when processing Binning: edges are neither an array nor a UniformBinning object");
  }

  variableIdx_ = detail::find_input_index(json.getRequired<std::string_view>("input"), context.inputs());
  if ( context.inputs().at(variableIdx_).type() == Variable::VarType::string ) {
    throw std::runtime_error("Binning cannot use string inputs as binning variables");
  }
  Content default_value{0.};
  const auto& flowbehavior = json.getRequiredValue("flow");
  flow_ = parse_flow_behavior(flowbehavior);
  if (flow_ == detail::FlowBehavior::value) {
      default_value = resolve_content(flowbehavior, context);
  }

  // set bin contents
  for (size_t i=0; i < content.Size(); ++i)
    contents_.push_back(resolve_content(content[i], context));
  contents_.push_back(std::move(default_value));
}

const Content& Binning::select(const std::vector<Variable::Type>& values) const
{
  std::size_t binIdx = find_bin_idx(values[variableIdx_], bins_, flow_, variableIdx_, "Binning");
  return contents_[binIdx];
}

double Binning::evaluate(const std::vector<Variable::Type>& values) const {
  return std::visit(node_evaluate{values}, select(values));
}
void Binning::evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const {
  detail::evaluate_content_array(select(values), values, output, size);
}

MultiBinning::MultiBinning(const JSONObject& json, const Correction& context)
{
  const auto& inputs = json.getRequired<rapidjson::Value::ConstArray>("inputs");

  const auto& edges = json.getRequired<rapidjson::Value::ConstArray>("edges");
  if (edges.Empty() || edges.Size() != inputs.Size())
    throw std::runtime_error("MultiBinning axes and inputs must have matching nonzero lengths");
  axes_.reserve(edges.Size());
  size_t idx {0};
  for (const auto& dimension : edges) {
    const auto& input = inputs[idx];
    if ( dimension.IsArray() ) { // non-uniform binning
      std::vector<double> dim_edges = parse_bin_edges(dimension.GetArray());
      if ( ! input.IsString() ) { throw std::runtime_error("invalid multibinning input type"); }
      if (!input.IsString()) throw std::runtime_error("invalid multibinning input type");
      size_t variableIdx = detail::find_input_index(input.GetString(), context.inputs());
      if ( context.inputs().at(variableIdx).type() == Variable::VarType::string ) {
        throw std::runtime_error("MultiBinning cannot use string inputs as binning variables");
      }
      axes_.push_back({variableIdx, 0, detail::NonUniformBins(std::move(dim_edges))});
    } else if ( dimension.IsObject() ) { // UniformBinning
      const JSONObject uniformBins{dimension.GetObject()};
      const auto n = uniformBins.getRequired<uint32_t>("n");
      if ( n == 0 ) {
        auto msg = "Error when processing MultiBinning: number of bins for dimension " + std::to_string(idx) + " is zero";
        throw std::runtime_error(std::move(msg));
      }
      const auto low = uniformBins.getRequired<double>("low");
      const auto high = uniformBins.getRequired<double>("high");
      if (!std::isfinite(low) || !std::isfinite(high) || !(low < high) || !std::isfinite(high-low))
        throw std::runtime_error("Uniform binning requires finite increasing bounds");
      if (!input.IsString()) throw std::runtime_error("invalid multibinning input type");
      size_t variableIdx = detail::find_input_index(input.GetString(), context.inputs());
      if ( context.inputs().at(variableIdx).type() == Variable::VarType::string ) {
        throw std::runtime_error("MultiBinning cannot use string inputs as binning variables");
      }
      axes_.push_back({variableIdx, 0, detail::UniformBins{n, low, high}});
    } else {
      auto msg = "Error when processing MultiBinning: edges for dimension " + std::to_string(idx) + " are neither an array nor a UniformBinning object";
      throw std::runtime_error (std::move(msg));
    }
    ++idx;
  }

  const auto& content = json.getRequired<rapidjson::Value::ConstArray>("content");
  size_t stride {1};
  --idx; // now corresponds to the last dimension
  for (auto it=axes_.rbegin(); it != axes_.rend(); ++it) {
    it->stride = stride;
    stride *= nbins(idx);
    --idx;
  }
  content_.reserve(content.Size() + 1); // + 1 for default value
  for (const auto& item : content) {
    content_.push_back(resolve_content(item, context));
  }
  if ( content_.size() != stride ) {
    throw std::runtime_error("Inconsistency in MultiBinning: number of content nodes does not match binning");
  }

  const auto& flowbehavior = json.getRequiredValue("flow");
  flow_ = parse_flow_behavior(flowbehavior);
  if (flow_ == detail::FlowBehavior::value) {
      content_.push_back(resolve_content(flowbehavior, context));
  }
}

const Content& MultiBinning::select(const std::vector<Variable::Type>& values) const
{
  size_t idx {0};
  size_t localidx {0};
  size_t dim {0};

  for (const auto& [variableIdx, stride, edgesVariant] : axes_) {
    localidx = find_bin_idx(values[variableIdx], edgesVariant, flow_, variableIdx, "MultiBinning");
    if ( localidx == nbins(dim) ) // find_bin_idx is indicating we need to return the default value
      return content_.back();
    idx += localidx * stride;
    ++dim;
  }

  return content_.at(idx);
}

double MultiBinning::evaluate(const std::vector<Variable::Type>& values) const {
  return std::visit(node_evaluate{values}, select(values));
}
void MultiBinning::evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const {
  detail::evaluate_content_array(select(values), values, output, size);
}

size_t MultiBinning::nbins(size_t dimension) const
{
  if ( const auto *bins = std::get_if<detail::UniformBins>(&axes_[dimension].bins) )
    return bins->n; // using uniform bins

  // otherwise we must have non-uniform bins
  const auto &bins = std::get<detail::NonUniformBins>(axes_[dimension].bins);
  return bins.size() - 1;
}

Category::Category(const JSONObject& json, const Correction& context)
{
  variableIdx_ = detail::find_input_index(json.getRequired<std::string_view>("input"), context.inputs());
  const auto& variable = context.inputs()[variableIdx_];
  if ( variable.type() == Variable::VarType::string ) {
    map_ = StrMap();
  } // (default-constructed as IntMap)
  for (const auto& kv_pair : json.getRequired<rapidjson::Value::ConstArray>("content"))
  {
    if ( ! (kv_pair.IsObject() && kv_pair.HasMember("key") && kv_pair.HasMember("value")) ) {
      throw std::runtime_error("Expected CategoryItem object");
    }
    if ( kv_pair["key"].IsString() ) {
      if ( variable.type() != Variable::VarType::string ) {
        throw std::runtime_error("Category got a key of type string, but its input is type " + variable.typeStr());
      }
      std::get<StrMap>(map_).try_emplace(kv_pair["key"].GetString(), resolve_content(kv_pair["value"], context));
    }
    else if ( kv_pair["key"].IsInt() ) {
      if ( variable.type() != Variable::VarType::integer ) {
        throw std::runtime_error("Category got a key of type int, but its input is type " + variable.typeStr());
      }
      std::get<IntMap>(map_).try_emplace(kv_pair["key"].GetInt(), resolve_content(kv_pair["value"], context));
    }
    else {
      throw std::runtime_error("Invalid key type in Category");
    }
  }

  const auto def = json.FindMember("default");
  if ( def != json.MemberEnd() && ! def->value.IsNull() ) {
    default_ = std::make_unique<Content>(resolve_content(def->value, context));
  }
}

const Content& Category::select(const std::vector<Variable::Type>& values) const {
  const Content* child = nullptr;
  if ( auto pval = std::get_if<std::string>(&values[variableIdx_]) ) {
    const auto& m = std::get<StrMap>(map_);
    auto it = m.find(*pval);
    if ( it != m.end() ) {
      child = &it->second;
    } else if ( default_ ) {
      child = default_.get();
    } else {
      throw std::out_of_range("Index not available in Category for input argument " + std::to_string(variableIdx_) + " val: " + *pval);
    }
  }
  else if ( auto pval = std::get_if<int64_t>(&values[variableIdx_]) ) {
    const auto& m = std::get<IntMap>(map_);
    auto it = m.find(*pval);
    if ( it != m.end() ) {
      child = &it->second;
    } else if ( default_ ) {
      child = default_.get();
    } else {
      throw std::out_of_range("Index not available in Category for input argument " + std::to_string(variableIdx_) + " val: " + std::to_string(*pval));
    }
  } else {
    throw std::runtime_error("Invalid variable type");
  }

  return *child;
}

double Category::evaluate(const std::vector<Variable::Type>& values) const {
  return std::visit(node_evaluate{values}, select(values));
}
void Category::evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const {
  detail::evaluate_content_array(select(values), values, output, size);
}

Correction::Correction(const JSONObject& json, std::shared_ptr<const PluginRegistry> registry, int schema_version) :
  name_(json.getRequired<const char *>("name")),
  description_(json.getOptional<const char*>("description").value_or("")),
  version_(json.getRequired<int>("version")),
  output_(json.getRequired<rapidjson::Value::ConstObject>("output"))
{
  if ( output_.type() != Variable::VarType::real ) { throw std::runtime_error("Outputs can only be real-valued"); }
  for (const auto& item : json.getRequired<rapidjson::Value::ConstArray>("inputs")) {
    if ( ! item.IsObject() ) { throw std::runtime_error("invalid input item type"); }
    inputs_.emplace_back(item.GetObject());
  }
  if ( const auto& items = json.getOptional<rapidjson::Value::ConstArray>("generic_formulas") ) {
    for (const auto& item : *items) {
      if ( ! item.IsObject() ) { throw std::runtime_error("invalid generic_formulas item type"); }
      formula_refs_.push_back(std::make_shared<Formula>(item.GetObject(), *this, true));
    }
  }

  const auto& output_json = json.getRequiredValue("output");
  if (output_json.HasMember("shape")) {
    if (schema_version != 3) throw std::runtime_error("Array output shape requires schema v3");
    const auto& shape = output_json["shape"];
    if (!shape.IsArray() || shape.Size() != 1 || !shape[0].IsUint64() || shape[0].GetUint64() == 0 ||
        shape[0].GetUint64() > std::numeric_limits<size_t>::max() / sizeof(double))
      throw std::runtime_error("Output shape must be one positive fixed dimension");
    output_size_ = static_cast<size_t>(shape[0].GetUint64());
  }
  if (schema_version == 2) {
    const auto check_v2 = [&](const auto& self, const rapidjson::Value& node) -> void {
      if (node.IsObject()) {
        if (node.HasMember("output_shape") ||
            (node.HasMember("nodetype") && node["nodetype"].IsString() && std::string_view(node["nodetype"].GetString()) == "array"))
          throw std::runtime_error("Array nodes require schema v3");
        for (auto it = node.MemberBegin(); it != node.MemberEnd(); ++it)
          if (std::string_view(it->name.GetString()) != "config" && std::string_view(it->name.GetString()) != "opaque") self(self, it->value);
      } else if (node.IsArray()) for (const auto& child : node.GetArray()) self(self, child);
    };
    check_v2(check_v2, json.getRequiredValue("data"));
  }
  if (detail::content_output_size(json.getRequiredValue("data")) != output_size_)
    throw std::runtime_error("Correction output shape does not match its data");

  // Factories need input/formula context while data_ is constructed. Retain
  // the set's registry snapshot before descending into any extension children.
  registry_ = registry ? std::move(registry) : std::make_shared<PluginRegistry>(default_plugin_registry());
  data_ = resolve_content(json.getRequiredValue("data"), *this);
  initialized_ = true;
}

void Correction::validate_inputs(const std::vector<Variable::Type>& values) const {
  if ( ! initialized_ ) {
    throw std::logic_error("Not initialized");
  }
  if ( values.size() != inputs_.size() ) {
    throw std::invalid_argument("Incorrect number of inputs (got " + std::to_string(values.size())
          + ", expected " + std::to_string(inputs_.size()) + ")");
  }
  for (size_t i=0; i < inputs_.size(); ++i) {
    inputs_[i].validate(values[i]);
  }

}

double Correction::evaluate(const std::vector<Variable::Type>& values) const {
  if (output_size_) throw std::runtime_error("Array correction requires evaluate_array");
  validate_inputs(values);
  return std::visit(node_evaluate{values}, data_);
}
std::vector<double> Correction::evaluate_array(const std::vector<Variable::Type>& values) const {
  std::vector<double> output(output_size_);
  evaluate_array(values, output.data(), output.size());
  return output;
}
void Correction::evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const {
  if (!output_size_) throw std::runtime_error("Scalar correction requires evaluate");
  if (!output || size < output_size_) throw std::invalid_argument("Array output buffer too small or null");
  validate_inputs(values);
  detail::evaluate_content_array(data_, values, output, output_size_);
}

CompoundCorrection::CompoundCorrection(const JSONObject& json, const CorrectionSet& context) :
  name_(json.getRequired<const char *>("name")),
  description_(json.getOptional<const char*>("description").value_or("")),
  output_(json.getRequired<rapidjson::Value::ConstObject>("output"))
{
  if ( output_.type() != Variable::VarType::real ) { throw std::runtime_error("Outputs can only be real-valued"); }
  if (json.getRequiredValue("output").HasMember("shape")) throw std::runtime_error("CompoundCorrection output must be scalar");
  for (const auto& item : json.getRequired<rapidjson::Value::ConstArray>("inputs")) {
    if ( ! item.IsObject() ) { throw std::runtime_error("invalid inputs item type"); }
    inputs_.emplace_back(item.GetObject());
  }
  for (const auto& item : json.getRequired<rapidjson::Value::ConstArray>("inputs_update")) {
    if ( ! item.IsString() ) { throw std::runtime_error("invalid inputs_update item type"); }
    size_t idx = input_index(item.GetString());
    if ( inputs_[idx].type() != Variable::VarType::real ) {
      throw std::runtime_error("CompoundCorrection updatable inputs must be real-valued");
    }
    inputs_update_.emplace_back(idx);
  }

  const auto& op = json.getRequired<std::string_view>("input_op");
  if ( op == "+" ) { input_op_ = UpdateOp::Add; }
  else if ( op == "*" ) { input_op_ = UpdateOp::Multiply; }
  else if ( op == "/" ) { input_op_ = UpdateOp::Divide; }
  else { throw std::runtime_error("Invalid CompoundCorrection input update op"); }

  const auto& out_op = json.getRequired<std::string_view>("output_op");
  if ( out_op == "+" ) { output_op_ = UpdateOp::Add; }
  else if ( out_op == "*" ) { output_op_ = UpdateOp::Multiply; }
  else if ( out_op == "/" ) { output_op_ = UpdateOp::Divide; }
  else if ( out_op == "last" ) { output_op_ = UpdateOp::Last; }
  else { throw std::runtime_error("Invalid CompoundCorrection output update op"); }

  for (const auto& item : json.getRequired<rapidjson::Value::ConstArray>("stack")) {
    if ( ! item.IsString() ) { throw std::runtime_error("Invalid type"); }
    Correction::Ref corr;
    try {
      corr = context.at(item.GetString());
    } catch (std::out_of_range& ex) {
      throw std::runtime_error("CompoundCorrection constituent "
          + std::string(item.GetString()) + " not found in the CorrectionSet");
    }
    if (corr->output_size()) throw std::runtime_error("CompoundCorrection cannot contain array outputs");
    std::vector<size_t> inmap;
    for (const auto& input : corr->inputs()) {
      inmap.push_back(input_index(input.name()));
    }
    stack_.emplace_back(std::move(inmap), corr);
  }
}

size_t CompoundCorrection::input_index(const std::string_view name) const {
  size_t idx = 0;
  for (const auto& var : inputs_) {
    if ( name == var.name() ) return idx;
    idx++;
  }
  throw std::runtime_error("Error: could not find variable " + std::string(name) + " in inputs");
}

double CompoundCorrection::evaluate(const std::vector<Variable::Type>& values) const {
  // Per-thread scratch storage. This call site is not re-entrant so we
  // can use a simpler implementation than for TransformScratch
  static thread_local std::vector<Variable::Type> ivalues;
  static thread_local std::vector<Variable::Type> cvalues;

  if ( values.size() != inputs_.size() ) {
    throw std::invalid_argument("Incorrect number of inputs (got " + std::to_string(values.size())
          + ", expected " + std::to_string(inputs_.size()) + ")");
  }
  for (size_t i=0; i < inputs_.size(); ++i) {
    inputs_[i].validate(values[i]);
  }
  ivalues = values;
  cvalues.reserve(values.size());

  double out = 0.;
  double sf = 0.;
  bool start{true};
  for(const auto& [inmap, corr] : stack_) {
    cvalues.clear();
    for(size_t pos : inmap) cvalues.push_back(ivalues[pos]);
    sf = corr->evaluate(cvalues);
    for(size_t pos : inputs_update_) {
      switch ( input_op_ ) {
        case UpdateOp::Add: std::get<double>(ivalues[pos]) += sf; break;
        case UpdateOp::Multiply: std::get<double>(ivalues[pos]) *= sf; break;
        case UpdateOp::Divide: std::get<double>(ivalues[pos]) /= sf; break;
        case UpdateOp::Last: throw std::logic_error("Illegal update op");
      }
    }
    if ( start ) { out = sf; start = false; }
    else {
      switch ( output_op_ ) {
        case UpdateOp::Add: out += sf; break;
        case UpdateOp::Multiply: out *= sf; break;
        case UpdateOp::Divide: out /= sf; break;
        case UpdateOp::Last: out = sf;
      }
    }
  }
  return out;
}

std::unique_ptr<CorrectionSet> CorrectionSet::from_file(const std::string& fn, const PluginRegistry& registry) {
  rapidjson::Document json;
  FILE* fp = fopen(fn.c_str(), "rb");
  if ( fp == nullptr ) {
    throw std::runtime_error("Failed to open file: " + fn);
  }
  constexpr unsigned char magicref[2] = {0x1f, 0x8b};
  unsigned char magic[2];
  if (fread(magic, sizeof *magic, 2, fp) != 2) {
    throw std::runtime_error("Failed to read file magic: " + fn);
  }
  rewind(fp);
  char readBuffer[65536];
  rapidjson::ParseResult ok;
  if (memcmp(magic, magicref, sizeof(magic)) == 0) {
    fclose(fp);
#ifdef WITH_ZLIB
    gzFile_s* fpz = gzopen(fn.c_str(), "r");
    rapidjson::GzFileReadStream is(fpz, readBuffer, sizeof(readBuffer));
    ok = json.ParseStream<rapidjson::kParseNanAndInfFlag>(is);
    gzclose(fpz);
#else
    throw std::runtime_error("Gzip-compressed JSON files are only supported if ZLIB is found when the package is built");
#endif
  } else {
    rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
    ok = json.ParseStream<rapidjson::kParseNanAndInfFlag>(is);
    fclose(fp);
  }
  if (!ok) {
    throw std::runtime_error(
        std::string("JSON parse error: ") + rapidjson::GetParseError_En(ok.Code())
        + " at offset " + std::to_string(ok.Offset())
        );
  }
  if ( ! json.IsObject() ) { throw std::runtime_error("Expected CorrectionSet object"); }
  return std::make_unique<CorrectionSet>(json, registry);
}

std::unique_ptr<CorrectionSet> CorrectionSet::from_string(const char * data, const PluginRegistry& registry) {
  rapidjson::Document json;
  rapidjson::ParseResult ok = json.Parse<rapidjson::kParseNanAndInfFlag>(data);
  if (!ok) {
    throw std::runtime_error(
        std::string("JSON parse error: ") + rapidjson::GetParseError_En(ok.Code())
        + " at offset " + std::to_string(ok.Offset())
        );
  }
  if ( ! json.IsObject() ) { throw std::runtime_error("Expected CorrectionSet object"); }
  return std::make_unique<CorrectionSet>(json, registry);
}

CorrectionSet::CorrectionSet(const JSONObject& json, const PluginRegistry& registry) {
  // Freeze the registration catalog once for this whole set, so concurrent
  // registration cannot change which factories its corrections observe.
  auto snapshot = std::make_shared<const PluginRegistry>(registry);
  schema_version_ = json.getRequired<int>("schema_version");
  if ( schema_version_ > evaluator_version ) {
    throw std::runtime_error("Evaluator is designed for schema v" + std::to_string(evaluator_version) + " and is not forward-compatible");
  }
  else if ( schema_version_ < 2 ) {
    throw std::runtime_error("Evaluator is designed for schema v" + std::to_string(evaluator_version) + " and is not backward-compatible");
  }
  description_ = json.getOptional<const char*>("description").value_or("");
  for (const auto& item : json.getRequired<rapidjson::Value::ConstArray>("corrections")) {
    if ( ! item.IsObject() ) { throw std::runtime_error("Expected Correction object"); }
    auto corr = std::make_shared<Correction>(item.GetObject(), snapshot, schema_version_);
    if ( corrections_.find(corr->name()) != corrections_.end() ) {
      throw std::runtime_error("Duplicate Correction name: " + corr->name());
    }
    corrections_[corr->name()] = corr;
  }
  if (auto items = json.getOptional<rapidjson::Value::ConstArray>("compound_corrections")) {
    for (const auto& item : *items) {
      if ( ! item.IsObject() ) { throw std::runtime_error("Expected CompoundCorrection object"); }
      auto corr = std::make_shared<CompoundCorrection>(item.GetObject(), *this);
      if ( compoundcorrections_.find(corr->name()) != compoundcorrections_.end() ) {
        throw std::runtime_error("Duplicate CompoundCorrection name: " + corr->name());
      }
      compoundcorrections_[corr->name()] = corr;
    }
  }
}

bool CorrectionSet::validate() {
  // TODO: validate with https://rapidjson.org/md_doc_schema.html
  return true;
}

// Internal bridge for the plugin SDK. Keep recursive parsing private instead
// of exposing RapidJSON or the concrete Content variant to provider libraries.
Content correction::detail::parse_plugin_child(const rapidjson::Value& json, const Correction& context) {
  return resolve_content(json, context);
}

Array::Array(const JSONObject& json, const Correction& context) {
  for (const auto& child : json.getRequired<rapidjson::Value::ConstArray>("content"))
    content_.push_back(resolve_content(child, context));
}
double Array::evaluate(const std::vector<Variable::Type>&) const {
  throw std::runtime_error("Array node requires evaluate_array");
}
void Array::evaluate_array(const std::vector<Variable::Type>& values, double* output, size_t size) const {
  if (size != content_.size()) throw std::runtime_error("Array component count mismatch");
  for (size_t i = 0; i < size; ++i) output[i] = std::visit(node_evaluate{values}, content_[i]);
}
void correction::detail::evaluate_content_array(const Content& content, const std::vector<Variable::Type>& values, double* output, size_t size) {
  std::visit([&](const auto& node) {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, Array> || std::is_same_v<T, ExtensionNode> ||
                  std::is_same_v<T, Binning> || std::is_same_v<T, MultiBinning> ||
                  std::is_same_v<T, Category> || std::is_same_v<T, Transform>)
      node.evaluate_array(values, output, size);
    else throw std::runtime_error("Scalar node cannot supply an array output");
  }, content);
}

// Infer and check every branch before constructing native nodes. Scalar = 0.
// No selected-branch runtime accident may silently change a correction's shape.
size_t correction::detail::content_output_size(const rapidjson::Value& json) {
  if (json.IsNumber()) return 0;
  if (!json.IsObject()) throw std::runtime_error("Invalid Content node type");
  JSONObject obj(json.GetObject());
  const auto type = obj.getRequired<std::string_view>("nodetype");
  if (type == "array") {
    const auto items = obj.getRequired<rapidjson::Value::ConstArray>("content");
    if (items.Empty()) throw std::runtime_error("Array must contain at least one component");
    for (const auto& child : items)
      if (content_output_size(child)) throw std::runtime_error("Array components must be scalar");
    return items.Size();
  }
  if (type == "extension") {
    size_t size = 0;
    if (auto shape = obj.getOptional<rapidjson::Value::ConstArray>("output_shape")) {
      if (shape->Size() != 1 || !(*shape)[0].IsUint64() || (*shape)[0].GetUint64() == 0 ||
          (*shape)[0].GetUint64() > std::numeric_limits<size_t>::max() / sizeof(double))
        throw std::runtime_error("Extension output_shape must be one positive dimension");
      size = static_cast<size_t>((*shape)[0].GetUint64());
    }
    if (auto children = obj.getOptional<rapidjson::Value::ConstArray>("children"))
      for (const auto& child : *children) content_output_size(child);
    return size;
  }
  if (type == "transform") {
    if (content_output_size(obj.getRequiredValue("rule"))) throw std::runtime_error("Transform rule must be scalar");
    return content_output_size(obj.getRequiredValue("content"));
  }
  if (type == "binning" || type == "multibinning" || type == "category") {
    const auto items = obj.getRequired<rapidjson::Value::ConstArray>("content");
    std::optional<size_t> size;
    const auto check = [&](const rapidjson::Value& child) {
      const auto current = content_output_size(child);
      if (size && *size != current) throw std::runtime_error("Graph branches have mismatched output shapes");
      size = current;
    };
    for (const auto& child : items) {
      if (type == "category") {
        if (!child.IsObject() || !child.HasMember("value")) throw std::runtime_error("Expected CategoryItem object");
        check(child["value"]);
      } else check(child);
    }
    if (type == "category") {
      auto it = obj.FindMember("default");
      if (it != obj.MemberEnd() && !it->value.IsNull()) check(it->value);
    } else {
      const auto& flow = obj.getRequiredValue("flow");
      if (!flow.IsString()) check(flow);
    }
    return size.value_or(0);
  }
  return 0; // Scalar formula/PRNG/LWTNN leaves are validated by their constructors.
}
