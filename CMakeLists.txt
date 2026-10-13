cmake_minimum_required(VERSION 3.25)

project(
  chroma
  VERSION 0.1.0
  DESCRIPTION "Native render graph for agent-generated video"
  LANGUAGES CXX
)

option(CHROMA_BUILD_TESTS "Build Chroma tests" ON)

add_library(chroma_core
  src/core/rational.cpp
  src/core/timeline.cpp
)
add_library(chroma::core ALIAS chroma_core)

target_compile_features(chroma_core PUBLIC cxx_std_20)
target_include_directories(chroma_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
  target_compile_options(chroma_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(chroma_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_executable(chroma_cli src/cli/main.cpp)
set_target_properties(chroma_cli PROPERTIES OUTPUT_NAME chroma)
target_link_libraries(chroma_cli PRIVATE chroma::core)

if(CHROMA_BUILD_TESTS)
  include(CTest)
  enable_testing()

  add_executable(chroma_timeline_tests tests/timeline_tests.cpp)
  target_link_libraries(chroma_timeline_tests PRIVATE chroma::core)
  add_test(NAME chroma_timeline_tests COMMAND chroma_timeline_tests)
endif()
