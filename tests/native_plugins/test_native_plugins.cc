#include <correction_plugin.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace correction;
namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("native test failed"); }
template<class F> void rejects(F run, const std::string& text) {
  try { run(); }
  catch (const std::exception& e) {
    if (std::string(e.what()).find(text) != std::string::npos) return;
    throw;
  }
  throw std::runtime_error("expected rejection: " + text);
}
std::string payload(const std::string& node, const std::string& type = "real") {
  return "{\"schema_version\":2,\"corrections\":[{\"name\":\"probe\",\"version\":1,"
    "\"inputs\":[{\"name\":\"x\",\"type\":\"" + type + "\"}],"
    "\"output\":{\"name\":\"y\",\"type\":\"real\"},\"data\":" + node + "}]}";
}
const std::string scale = R"({"nodetype":"extension","type":"org.example.scale","version":1,"config":{"input":"x","factor":2}})";
const std::string formula = R"({"nodetype":"formula","expression":"x*x","parser":"TFormula","variables":["x"]})";
std::string offset(const std::string& child) {
  return R"({"nodetype":"extension","type":"org.example.offset","version":1,"config":{"amount":3},"children":[)" + child + "]}";
}
double evaluate(const std::string& node, const PluginRegistry& registry, double x=2.0) {
  return CorrectionSet::from_string(payload(node).c_str(), registry)->at("probe")->evaluate({x});
}
}
int main(int argc, char** argv) {
  check(argc == 5);
  PluginRegistry registry;
  rejects([&] { evaluate(scale, registry); }, "Missing plugin");
  rejects([&] { registry.load_library(argv[2]); }, "Incompatible plugin API");
  rejects([&] { registry.load_library(argv[3]); }, "registration failed");
  check(!registry.contains("org.example.rollback", 1));
  rejects([&] { registry.load_library(argv[4]); }, "Incompatible plugin build");
  registry.load_library(argv[1]);
  registry.load_library(argv[1]); // Idempotent.
  check(evaluate(scale, registry) == 4.0);
  check(evaluate(offset(formula), registry) == 7.0);
  check(evaluate(offset(scale), registry) == 7.0);
  check(evaluate(R"({"nodetype":"binning","input":"x","edges":[0.0,10.0],"flow":"error","content":[)" + scale + "]}", registry) == 4.0);
  check(evaluate(R"({"nodetype":"transform","input":"x","rule":5,"content":)" + scale + "}", registry) == 10.0);
  check(evaluate(R"({"nodetype":"multibinning","inputs":["x"],"edges":[[0.0,10.0]],"flow":"error","content":[)" + scale + "]}", registry) == 4.0);
  check(evaluate(R"({"nodetype":"binning","input":"x","edges":[0.0,1.0],"content":[0.0],"flow":)" + scale + "}", registry) == 4.0);
  auto category_payload = payload(R"({"nodetype":"category","input":"label","content":[{"key":"a","value":)" + scale + "}]}");
  category_payload.replace(category_payload.find("\"type\":\"real\"}"), 14, "\"type\":\"real\"},{\"name\":\"label\",\"type\":\"string\"}");
  auto category = CorrectionSet::from_string(category_payload.c_str(), registry);
  check(category->at("probe")->evaluate({2.0, std::string("a")})==4.0);
  check(PluginConfig(R"({"x":101.32762799999999})").real("x")==101.32762799999999);
  PluginConfig typed(R"({"n":9223372036854775807,"s":"text"})");
  check(typed.integer("n")==INT64_MAX);
  check(typed.string("s")=="text");
  rejects([&] { typed.real("s"); }, "expects real");
  rejects([&] { PluginConfig invalid(R"({"x":1,"x":2})"); }, "Duplicate");
  auto v2 = scale;
  v2.replace(v2.find("\"version\":1"), 11, "\"version\":2");
  rejects([&] { evaluate(v2, registry); }, "version 2");
  rejects([&] { CorrectionSet::from_string(payload(scale,"int").c_str(),registry); }, "type mismatch");
  rejects([&] { evaluate(R"({"nodetype":"extension","type":"org.example.scale","version":1,"config":{"input":"x"}})",registry); }, "missing");
  rejects([&] { evaluate(R"({"nodetype":"extension","type":"org.example.scale","version":1,"config":{"input":"x","factor":"bad"}})",registry); }, "expects real");
  rejects([&] { evaluate(R"({"nodetype":"extension","type":"org.example.scale","version":1,"config":{"input":"x","factor":2,"extra":1}})",registry); }, "Unknown plugin config");
  rejects([&] { evaluate(R"({"nodetype":"extension","type":"org.example.offset","version":1,"config":{"amount":1}})",registry); }, "one child");
  rejects([&] { evaluate(R"({"nodetype":"extension","type":"org.example.scale","version":1,"config":{},"extra":1})",registry); }, "Unknown or duplicate extension");
  rejects([&] { registry.register_node("binning",1,[](const auto&,const auto&)->std::shared_ptr<const PluginNode>{return nullptr;}); }, "namespaced");
  registry.register_node("org.example.null",1,[](const auto&,const auto&)->std::shared_ptr<const PluginNode>{return nullptr;});
  rejects([&] { registry.register_node("org.example.null",1,[](const auto&,const auto&)->std::shared_ptr<const PluginNode>{return nullptr;}); }, "Duplicate plugin");
  rejects([&] { evaluate(R"({"nodetype":"extension","type":"org.example.null","version":1,"config":{}})",registry); }, "returned null");
  auto snapshot = CorrectionSet::from_string(payload(offset(scale)).c_str(), registry);
  auto correction = snapshot->at("probe");
  snapshot.reset();
  std::atomic<bool> ok{true};
  std::vector<std::thread> workers;
  for(int t=0;t<8;++t) workers.emplace_back([&] {
    for(int i=0;i<1000;++i) if(correction->evaluate({2.0})!=7.0) ok=false;
  });
  for(auto& thread:workers) thread.join();
  check(ok);
  Correction::Ref retained;
  {
    PluginRegistry temporary;
    temporary.load_library(argv[1]);
    retained=CorrectionSet::from_string(payload(offset(scale)).c_str(), temporary)->at("probe");
  }
  check(retained->evaluate({4.0})==11.0);
  check(evaluate(formula, PluginRegistry())==4.0);
  std::cout << "Native registry, loader, nesting, errors, lifetime and concurrency passed\n";
}
