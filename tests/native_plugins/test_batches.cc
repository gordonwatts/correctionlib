#include <correction_plugin.h>
#include <future>
#include <limits>
#include <stdexcept>
using namespace correction;
void check(bool ok) { if (!ok) throw std::runtime_error("batch check failed"); }
template<class F> void rejects(F f) {
  try { f(); } catch (const std::exception&) { return; }
  throw std::runtime_error("expected batch rejection");
}
int main(int argc, char** argv) {
  check(argc == 2);
  auto set = CorrectionSet::from_string(R"({"schema_version":2,"corrections":[{"name":"h","version":1,"inputs":[{"name":"x","type":"real"}],"output":{"name":"y","type":"real"},"data":{"nodetype":"binning","input":"x","edges":[0.0,1.0,2.0],"content":[3.0,7.0],"flow":"clamp"}}]})");
  auto c = set->at("h");
  double x[]{-1.,0.,0.5,1.,2.,std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
  double out[7];
  c->evaluate_batch({BatchInput::reals(x,7)},7,out,7);
  for (size_t i=0;i<7;++i) check(out[i] == c->evaluate({x[i]}));
  c->evaluate_batch({BatchInput::reals(x+6,7,-1)},7,out,7);
  for (size_t i=0;i<7;++i) check(out[i] == c->evaluate({x[6-i]}));
  c->evaluate_batch({BatchInput::reals(x,7,0)},7,out,7);
  for (double v : out) check(v == 3.);
  c->evaluate_batch({BatchInput(1.0)},7,out,7);
  for (double v : out) check(v == 7.);
  c->evaluate_batch({BatchInput::reals(nullptr,0)},0,nullptr,0);
  rejects([&]{ c->evaluate_batch({},0,nullptr,0); });
  rejects([&]{ c->evaluate_batch({BatchInput(int64_t{1})},0,nullptr,0); });
  rejects([&]{ c->evaluate_batch({BatchInput::reals(nullptr,7)},7,out,7); });
  rejects([&]{ c->evaluate_batch({BatchInput::reals(x,6)},7,out,7); });
  rejects([&]{ c->evaluate_batch({BatchInput::reals(x,7)},7,out,6); });
  rejects([&]{ c->evaluate_batch({BatchInput::reals(x,7,std::numeric_limits<std::ptrdiff_t>::max())},7,out,7); });
  // Surviving correction owns its graph; concurrent calls have disjoint outputs.
  set.reset();
  auto run=[&]{ double local[7]; c->evaluate_batch({BatchInput::reals(x,7)},7,local,7); return local[3]; };
  auto a=std::async(std::launch::async,run); auto b=std::async(std::launch::async,run);
  check(a.get()==7. && b.get()==7.);
  PluginRegistry registry; registry.load_library(argv[1]);
  auto probe=CorrectionSet::from_string(R"({"schema_version":2,"corrections":[{"name":"p","version":1,"inputs":[{"name":"x","type":"real"}],"output":{"name":"y","type":"real"},"data":{"nodetype":"extension","type":"test.batch","version":1,"config":{}}}]})",registry)->at("p");
  rejects([&]{ probe->evaluate({1.}); });
  probe->evaluate_batch({BatchInput::reals(x,7)},7,out,7);
  for(size_t i=0;i<6;++i) check(out[i]==2*x[i]);
}
