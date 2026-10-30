#include <correction_plugin.h>
#include <future>
#include <limits>
#include <stdexcept>
using namespace correction;
void check(bool ok) { if(!ok) throw std::runtime_error("batch array check failed"); }
template<class F> void rejects(F f) { try{f();}catch(const std::exception&){return;}throw std::runtime_error("expected batch array rejection"); }
std::string payload(const std::string& node,size_t s) {
  return R"({"schema_version":3,"corrections":[{"name":"a","version":1,"inputs":[{"name":"x","type":"real"}],"output":{"name":"y","type":"real","shape":[)"+std::to_string(s)+R"(]},"data":)"+node+"}]}";
}
int main(int argc,char** argv) {
  std::vector<size_t> sizes;for(size_t n=0;n<=16;++n)sizes.push_back(n);for(size_t n:{1000,10000,100000})sizes.push_back(n);
  for(size_t s:{1,2,16}) {
    std::string array=R"({"nodetype":"array","content":[)";for(size_t j=0;j<s;++j){if(j)array+=",";array+=std::to_string(j+1.);}array+="]}";
    std::string histogram=R"({"nodetype":"binning","input":"x","edges":[0.0,1.0,2.0],"content":[)"+array+","+array+R"(],"flow":"clamp"})";
    auto c=CorrectionSet::from_string(payload(histogram,s).c_str())->at("a");
    for(auto n:sizes) {
      std::vector<double>x(n,0.25),out(n*s+3,-99.);
      c->evaluate_batch_array({BatchInput::reals(x.data(),n)},n,out.data(),out.size());
      for(size_t i=0;i<n;++i)for(size_t j=0;j<s;++j)check(out[i*s+j]==j+1.);
      check(out[n*s]==-99.); // Capacity greater than logical output must not change row stride.
    }
    c->evaluate_batch_array({BatchInput::reals(nullptr,0)},0,nullptr,0);
    double x[]{1.,2.}, out[32];
    rejects([&]{c->evaluate_batch_array({BatchInput::reals(x,2)},2,out,2*s-1);});
    rejects([&]{c->evaluate_batch_array({BatchInput(int64_t{0})},0,nullptr,0);});
    rejects([&]{c->evaluate_batch({BatchInput(0.)},1,out,32);});
    rejects([&]{c->evaluate_batch_array({BatchInput(0.)},std::numeric_limits<size_t>::max(),out,32);});
    if(argc>1) {
      PluginRegistry registry;registry.load_library(argv[1]);
      std::string plugin=R"({"nodetype":"extension","type":"test.array_batch","version":1,"output_shape":[)"+std::to_string(s)+R"(],"config":{"input":"x","size":)"+std::to_string(s)+"}}";
      auto a=CorrectionSet::from_string(payload(plugin,s).c_str(),registry)->at("a");
      rejects([&]{a->evaluate_array({1.});});
      for(auto n:sizes) {
        std::vector<double>data(n,3.),result(n*s);
        a->evaluate_batch_array({BatchInput::reals(data.data(),n)},n,result.data(),result.size());
        for(size_t i=0;i<n;++i)for(size_t j=0;j<s;++j)check(result[i*s+j]==3.+j);
      }
      auto run=[&]{double result[32];a->evaluate_batch_array({BatchInput::reals(x+1,2,-1)},2,result,32);return result[0];};
      auto f=std::async(std::launch::async,run);check(f.get()==2.);
    }
  }
}
