cmake_minimum_required(VERSION 3.15)
project(native_plugin_consumer LANGUAGES CXX)
find_package(correctionlib CONFIG REQUIRED)
find_library(EXAMPLE_NODE_LIBRARY NAMES example_correction_nodes
  PATH_SUFFIXES example_correction_nodes REQUIRED)
add_executable(native_plugin_consumer ../../../tests/native_plugins/test_linked_plugin.cc)
target_link_libraries(native_plugin_consumer PRIVATE correctionlib ${EXAMPLE_NODE_LIBRARY})
