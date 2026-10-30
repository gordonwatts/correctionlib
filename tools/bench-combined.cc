#include <correction_plugin.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace correction;
std::string node(size_t size, bool array, bool high) {
  std::string s=array ? R"({"nodetype":"array","content":[)" : "";
  for(size_t j=0;j<(array?size:1);++j) { if(j) s+=","; s+=std::to_string((high?7.:3.)+j); }
  return s+(array?"]}":"");
}
std::string payload(size_t size, bool array, size_t component=0) {
  std::string lo=array?node(size,true,false):std::to_string(3.+component);
  std::string hi=array?node(size,true,true):std::to_string(7.+component);
  return R"({"schema_version":3,"corrections":[{"name":"h","version":1,"inputs":[{"name":"x","type":"real"}],"output":{"name":"y","type":"real")"+
    (array?",\"shape\":["+std::to_string(size)+"]":"")+
    R"(},"data":{"nodetype":"binning","input":"x","edges":[0.0,1.0,2.0],"content":[)"+lo+","+hi+R"(],"flow":"clamp"}}]})";
}

int main(int argc,char**argv) {
  if(argc!=2) throw std::runtime_error("pass independent array provider path");
  PluginRegistry registry;registry.load_library(argv[1]);
  std::cout<<"fixture,n,s,round,mode,repeats,ns_per_call\n";
  std::vector<size_t> sizes;for(size_t n=1;n<=16;++n)sizes.push_back(n);for(size_t n:{1000,10000,100000})sizes.push_back(n);
  for(const std::string fixture:{"histogram","plugin"})for(size_t s:{1,2,16}) {
    std::string text=payload(s,true);
    if(fixture=="plugin") {
      auto data=text.find(R"("data":)")+7;
      text=text.substr(0,data)+R"({"nodetype":"extension","type":"org.example.components","version":1,"output_shape":[)"+std::to_string(s)+R"(],"config":{"input":"x","size":)"+std::to_string(s)+"}}}]}";
    }
    auto c=CorrectionSet::from_string(text.c_str(),registry)->at("h");
    for(auto n:sizes) {
      std::vector<double>x(n),out(n*s),expected(n*s);
      for(size_t row=0;row<n;++row){x[row]=(row%300)*0.01;c->evaluate_array({x[row]},expected.data()+row*s,s);}
      std::vector<BatchInput>inputs{BatchInput::reals(x.data(),n)};
      size_t repeats=std::max<size_t>(1,50000/(n*s));
      for(int round=0;round<7;++round)for(int k=0;k<2;++k) {
        bool batch=(round+k)%2==0;
        auto run=[&]{if(batch)c->evaluate_batch_array(inputs,n,out.data(),out.size());else for(size_t row=0;row<n;++row)c->evaluate_array({x[row]},out.data()+row*s,s);};
        run();if(out!=expected)throw std::runtime_error("combined timing mismatch");
        auto start=std::chrono::steady_clock::now();for(size_t r=0;r<repeats;++r)run();auto end=std::chrono::steady_clock::now();
        if(out!=expected)throw std::runtime_error("combined timing mismatch");
        std::cout<<fixture<<','<<n<<','<<s<<','<<round<<','<<(batch?"batch_array":"point_loop")<<','<<repeats<<','<<std::chrono::duration<double,std::nano>(end-start).count()/repeats<<'\n';
      }
    }
  }
}
