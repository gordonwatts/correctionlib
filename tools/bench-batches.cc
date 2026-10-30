#include <correction.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace correction;
int main() {
  auto c = CorrectionSet::from_string(R"({"schema_version":2,"corrections":[{"name":"h","version":1,"inputs":[{"name":"x","type":"real"}],"output":{"name":"y","type":"real"},"data":{"nodetype":"binning","input":"x","edges":[0.0,1.0,2.0],"content":[3.0,7.0],"flow":"clamp"}}]})")->at("h");
  std::cout << "n,round,mode,repeats,ns_per_call\n";
  std::vector<size_t> sizes; for(size_t n=1;n<=16;++n) sizes.push_back(n);
  for(size_t n : {1000,10000,100000}) sizes.push_back(n);
  for(auto n : sizes) {
    std::vector<double> x(n), out(n), expected(n); for(size_t i=0;i<n;++i) x[i]=(i%300)*0.01;
    for(size_t i=0;i<n;++i) expected[i]=c->evaluate({x[i]});
    std::vector<BatchInput> inputs{BatchInput::reals(x.data(),n)};
    size_t repeats=std::max<size_t>(5,200000/n);
    for(int round=0;round<7;++round) for(int mode=0;mode<2;++mode) {
      bool batch=(mode+round)%2==0;
      auto run=[&]{ if(batch) c->evaluate_batch(inputs,n,out.data(),n);
                   else for(size_t i=0;i<n;++i) out[i]=c->evaluate({x[i]}); };
      run(); if(out!=expected) throw std::runtime_error("benchmark mismatch");
      auto start=std::chrono::steady_clock::now();
      for(size_t r=0;r<repeats;++r) run();
      auto end=std::chrono::steady_clock::now();
      if(out!=expected) throw std::runtime_error("benchmark mismatch");
      std::cout << n << ',' << round << ',' << (batch?"batch":"scalar_loop") << ',' << repeats << ','
                << std::chrono::duration<double,std::nano>(end-start).count()/repeats << '\n';
    }
  }
}
