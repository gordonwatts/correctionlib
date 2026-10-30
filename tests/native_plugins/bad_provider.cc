#include <correction_plugin.h>
#include <stdexcept>

// One source builds deliberately incompatible/throwing provider fixtures.
// The loader must reject each before exposing any partially registered factory.
extern "C" int correctionlib_plugin_api_version() {
#ifdef wrong_api
  return -1;
#else
  return correction::plugin_api_version;
#endif
}

extern "C" void correctionlib_register_nodes_v4(correction::PluginRegistry& registry) {
  // Stage one factory before failing so the test can detect a broken rollback.
  registry.register_node(
      "org.example.rollback", 1,
      [](const auto&, const auto&) -> std::shared_ptr<const correction::PluginNode> {
        return nullptr;
      });
  throw std::runtime_error("registration failed");
}

extern "C" const char* correctionlib_plugin_build_id() {
#ifdef wrong_build
  return "different-core-build";
#else
  return correction::plugin_build_id().c_str();
#endif
}
