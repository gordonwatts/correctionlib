#include <correction_plugin.h>
#include <future>
#include <stdexcept>
using namespace correction;
void check(bool ok) { if(!ok) throw std::runtime_error("array check failed"); }
template<class F> void rejects(F f) { try { f(); } catch(const std::exception&) { return; } throw std::runtime_error("expected array rejection"); }
std::string payload(const std::string& node, size_t size) {
  return R"({"schema_version":3,"corrections":[{"name":"a","version":1,"inputs":[{"name":"x","type":"real"}],"output":{"name":"y","type":"real","shape":[)" + std::to_string(size) + R"(]},"data":)" + node + "}]}";
}
int main(int argc, char** argv) {
  auto set=CorrectionSet::from_string(payload(R"({"nodetype":"array","content":[1.0,2.0]})",2).c_str());
  auto c=set->at("a"); set.reset();
  check(c->output_size()==2 && c->evaluate_array({0.})==std::vector<double>({1.,2.}));
  rejects([&]{c->evaluate({0.});});
  double out[2]; rejects([&]{c->evaluate_array({0.},out,1);});
  rejects([&]{c->evaluate_array({0.},nullptr,2);});
  rejects([&]{c->evaluate_array({int64_t{0}});});
  auto run=[&]{ return c->evaluate_array({3.}); };
  auto a=std::async(std::launch::async,run); auto b=std::async(std::launch::async,run);
  check(a.get()==b.get());
  if(argc>1) {
    PluginRegistry registry; registry.load_library(argv[1]);
    auto plugin=CorrectionSet::from_string(payload(R"({"nodetype":"extension","type":"org.example.components","version":1,"output_shape":[2],"config":{"input":"x","size":2}})",2).c_str(),registry)->at("a");
    check(plugin->evaluate_array({3.})==std::vector<double>({3.,4.}));
    rejects([&]{plugin->evaluate({3.});});
  }
}
