cmake_minimum_required(VERSION 3.25)

project(
  chroma
  VERSION 0.1.0
  DESCRIPTION "Native render graph for agent-generated video"
  LANGUAGES CXX
)

option(CHROMA_BUILD_TESTS "Build Chroma tests" ON)

include(FetchContent)
find_package(nlohmann_json 3.11.3 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(chroma_core
  src/core/composition.cpp
  src/core/execution_plan.cpp
  src/core/rational.cpp
  src/core/render.cpp
  src/core/timeline.cpp
)
add_library(chroma::core ALIAS chroma_core)

target_compile_features(chroma_core PUBLIC cxx_std_20)
target_include_directories(chroma_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(chroma_core PRIVATE nlohmann_json::nlohmann_json)

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

  add_executable(chroma_composition_tests tests/composition_tests.cpp)
  target_link_libraries(chroma_composition_tests PRIVATE chroma::core)
  add_test(NAME chroma_composition_tests COMMAND chroma_composition_tests)

  add_executable(chroma_render_tests tests/render_tests.cpp)
  target_link_libraries(chroma_render_tests PRIVATE chroma::core)
  add_test(NAME chroma_render_tests COMMAND chroma_render_tests)

  add_executable(chroma_execution_plan_tests tests/execution_plan_tests.cpp)
  target_link_libraries(chroma_execution_plan_tests PRIVATE chroma::core)
  add_test(NAME chroma_execution_plan_tests COMMAND chroma_execution_plan_tests)

  add_test(
    NAME chroma_cli_validates_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/solid.json
  )

  add_test(
    NAME chroma_cli_validates_layered_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/layered.json
  )

  add_test(
    NAME chroma_cli_inspects_example_plan
    COMMAND chroma_cli inspect-plan ${CMAKE_CURRENT_SOURCE_DIR}/examples/solid.json --frame 0
  )
  set_tests_properties(chroma_cli_inspects_example_plan PROPERTIES
    PASS_REGULAR_EXPRESSION "composite_solid"
  )
endif()
