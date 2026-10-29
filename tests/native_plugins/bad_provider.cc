#include <correction_plugin.h>
#include <stdexcept>
extern "C" int correctionlib_plugin_api_version() {
#ifdef wrong_api
  return -1;
#else
  return correction::plugin_api_version;
#endif
}
extern "C" void correctionlib_register_nodes_v1(correction::PluginRegistry& registry) {
  registry.register_node("org.example.rollback", 1, [](const auto&, const auto&)
      -> std::shared_ptr<const correction::PluginNode> { return nullptr; });
  throw std::runtime_error("registration failed");
}

extern "C" const char* correctionlib_plugin_build_id() {
#ifdef wrong_build
  return "different-core-build";
#else
  return correction::plugin_build_id().c_str();
#endif
}
