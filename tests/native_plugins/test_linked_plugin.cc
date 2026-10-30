#include <correction_plugin.h>
#include <stdexcept>
// This executable links the provider directly: there is no runtime library
// search, Python environment or entry-point metadata involved.
extern "C" void correctionlib_register_nodes_v4(correction::PluginRegistry&);

int main() {
  correction::PluginRegistry registry;

  // Explicit registration feeds the same registry API as dynamic loading.
  correctionlib_register_nodes_v4(registry);
  auto set = correction::CorrectionSet::from_string(R"({"schema_version":2,"corrections":[{
    "name":"scale","version":1,"inputs":[{"name":"x","type":"real"}],
    "output":{"name":"y","type":"real"},"data":{"nodetype":"extension",
    "type":"org.example.scale","version":1,"config":{"input":"x","factor":2}}}]})",
                                                    registry);
  if (set->at("scale")->evaluate({3.0}) != 6.0)
    throw std::runtime_error("linked plugin result");
}
