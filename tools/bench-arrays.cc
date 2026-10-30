#include <correction.h>
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
int main() {
  std::cout<<"n,s,round,mode,repeats,ns_per_call\n";
  std::vector<size_t> sizes; for(size_t n=1;n<=16;++n) sizes.push_back(n);
  for(size_t n:{1000,10000,100000}) sizes.push_back(n);
  for(size_t s:{1,2,16}) {
    auto a=CorrectionSet::from_string(payload(s,true).c_str())->at("h");
    std::vector<Correction::Ref> scalars;
    for(size_t j=0;j<s;++j) scalars.push_back(CorrectionSet::from_string(payload(s,false,j).c_str())->at("h"));
    for(auto n:sizes) {
      std::vector<double> x(n),out(n*s),expected(n*s);
      for(size_t i=0;i<n;++i) {x[i]=(i%300)*0.01; for(size_t j=0;j<s;++j) expected[i*s+j]=scalars[j]->evaluate({x[i]});}
      size_t repeats=std::max<size_t>(1,50000/(n*s));
      for(int round=0;round<7;++round) for(int k=0;k<2;++k) {
        bool array=(round+k)%2==0;
        auto run=[&]{for(size_t i=0;i<n;++i) {
          if(array) a->evaluate_array({x[i]},out.data()+i*s,s);
          else for(size_t j=0;j<s;++j) out[i*s+j]=scalars[j]->evaluate({x[i]});
        }};
        run(); if(out!=expected) throw std::runtime_error("array timing mismatch");
        auto start=std::chrono::steady_clock::now();for(size_t r=0;r<repeats;++r)run();auto end=std::chrono::steady_clock::now();
        if(out!=expected) throw std::runtime_error("array timing mismatch");
        std::cout<<n<<','<<s<<','<<round<<','<<(array?"array":"scalar_components")<<','<<repeats<<','<<std::chrono::duration<double,std::nano>(end-start).count()/repeats<<'\n';
      }
    }
  }
}
