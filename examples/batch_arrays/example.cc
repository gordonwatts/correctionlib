#include <correction.h>
#include <iostream>
#include <stdexcept>
int main(int argc,char**argv) {
  if(argc!=2) throw std::runtime_error("usage: batch_arrays payload.json");
  auto cset=correction::CorrectionSet::from_file(argv[1]);
  std::vector<double>x{0.25,1.25,0.5},scalar(x.size()),rows(x.size()*2);
  std::vector<correction::BatchInput>inputs{correction::BatchInput::reals(x.data(),x.size())};
  cset->at("scalar")->evaluate_batch(inputs,x.size(),scalar.data(),scalar.size());
  auto pair=cset->at("pair")->evaluate_array({0.25});
  cset->at("pair")->evaluate_batch_array(inputs,x.size(),rows.data(),rows.size());
  if(scalar!=std::vector<double>({1.,3.,1.}) || pair!=std::vector<double>({1.,2.}) || rows!=std::vector<double>({1.,2.,3.,4.,1.,2.}))
    throw std::runtime_error("example output mismatch");
  for(size_t row=0;row<x.size();++row) std::cout<<scalar[row]<<" : "<<rows[row*2]<<", "<<rows[row*2+1]<<'\n';
}
