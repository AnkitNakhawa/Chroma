cmake_minimum_required(VERSION 3.25)

project(
  chroma
  VERSION 0.1.0
  DESCRIPTION "Native render graph for agent-generated video"
  LANGUAGES CXX
)

option(CHROMA_BUILD_TESTS "Build Chroma tests" ON)
option(CHROMA_ENABLE_FFMPEG "Enable FFmpeg media support when development libraries are available" ON)
option(CHROMA_ENABLE_TEXT "Enable HarfBuzz/FreeType text support when available" ON)

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

find_package(PkgConfig QUIET)

set(CHROMA_HAS_FFMPEG 0)
if(CHROMA_ENABLE_FFMPEG)
  if(PkgConfig_FOUND)
    pkg_check_modules(CHROMA_FFMPEG QUIET IMPORTED_TARGET
      libavformat
      libavcodec
      libavutil
      libswscale
    )
  endif()
  if(TARGET PkgConfig::CHROMA_FFMPEG)
    set(CHROMA_HAS_FFMPEG 1)
    message(STATUS "Chroma FFmpeg media support: enabled")
  else()
    message(WARNING "FFmpeg development libraries were not found; media support is disabled")
  endif()
else()
  message(STATUS "Chroma FFmpeg media support: disabled by configuration")
endif()

set(CHROMA_HAS_TEXT 0)
if(CHROMA_ENABLE_TEXT AND PkgConfig_FOUND)
  pkg_check_modules(CHROMA_TEXT QUIET IMPORTED_TARGET harfbuzz freetype2)
  if(TARGET PkgConfig::CHROMA_TEXT)
    set(CHROMA_HAS_TEXT 1)
    message(STATUS "Chroma HarfBuzz/FreeType text support: enabled")
  else()
    message(WARNING "HarfBuzz/FreeType development libraries were not found; text support is disabled")
  endif()
else()
  message(STATUS "Chroma HarfBuzz/FreeType text support: disabled")
endif()

if(CHROMA_HAS_FFMPEG)
  find_program(CHROMA_FFMPEG_EXECUTABLE ffmpeg)
endif()

add_library(chroma_core
  src/core/composition.cpp
  src/core/effect.cpp
  src/core/execution_plan.cpp
  src/core/prepared_resource.cpp
  src/core/rational.cpp
  src/core/render.cpp
  src/core/timeline.cpp
  src/media/decode.cpp
  src/media/encode.cpp
  src/media/frame.cpp
  src/media/media.cpp
  src/text/text.cpp
)
add_library(chroma::core ALIAS chroma_core)

target_compile_features(chroma_core PUBLIC cxx_std_20)
target_include_directories(chroma_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(chroma_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(chroma_core PRIVATE
  CHROMA_HAS_FFMPEG=${CHROMA_HAS_FFMPEG}
  CHROMA_HAS_TEXT=${CHROMA_HAS_TEXT}
)
if(CHROMA_HAS_FFMPEG)
  target_link_libraries(chroma_core PRIVATE PkgConfig::CHROMA_FFMPEG)
endif()
if(CHROMA_HAS_TEXT)
  target_link_libraries(chroma_core PRIVATE PkgConfig::CHROMA_TEXT)
endif()

if(MSVC)
  target_compile_options(chroma_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(chroma_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_executable(chroma_cli src/cli/main.cpp)
set_target_properties(chroma_cli PROPERTIES OUTPUT_NAME chroma)
target_link_libraries(chroma_cli PRIVATE chroma::core)

if(CHROMA_HAS_FFMPEG AND CHROMA_FFMPEG_EXECUTABLE)
  set(CHROMA_BENCHMARK_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/benchmarks)
  set(CHROMA_BENCHMARK_MEDIA ${CHROMA_BENCHMARK_DIRECTORY}/color-segments-640x360.mkv)
  add_custom_command(
    OUTPUT ${CHROMA_BENCHMARK_MEDIA}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CHROMA_BENCHMARK_DIRECTORY}
    COMMAND ${CHROMA_FFMPEG_EXECUTABLE} -y -hide_banner -loglevel error
            -f lavfi -i color=c=blue:size=640x360:rate=30:duration=2
            -f lavfi -i color=c=red:size=640x360:rate=30:duration=2
            -filter_complex [0:v][1:v]concat=n=2:v=1:a=0
            -c:v ffv1 ${CHROMA_BENCHMARK_MEDIA}
    VERBATIM
  )
  add_custom_target(chroma_benchmark_fixture DEPENDS ${CHROMA_BENCHMARK_MEDIA})
  configure_file(benchmarks/video-composition.json.in
                 ${CHROMA_BENCHMARK_DIRECTORY}/video-composition.json @ONLY)
  configure_file(benchmarks/fused-video-composition.json.in
                 ${CHROMA_BENCHMARK_DIRECTORY}/fused-video-composition.json @ONLY)
endif()

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

  add_executable(chroma_execution_plan_tests tests/execution_plan_tests.cpp)
  target_link_libraries(chroma_execution_plan_tests PRIVATE chroma::core)
  add_test(NAME chroma_execution_plan_tests COMMAND chroma_execution_plan_tests)

  add_executable(chroma_effect_tests tests/effect_tests.cpp)
  target_link_libraries(chroma_effect_tests PRIVATE chroma::core)
  add_test(NAME chroma_effect_tests COMMAND chroma_effect_tests)

  add_executable(chroma_media_tests tests/media_tests.cpp)
  target_link_libraries(chroma_media_tests PRIVATE chroma::core)

  add_executable(chroma_text_tests tests/text_tests.cpp)
  target_link_libraries(chroma_text_tests PRIVATE chroma::core)
  if(CHROMA_HAS_TEXT)
    find_file(CHROMA_TEST_FONT
      NAMES DejaVuSans.ttf Arial.ttf
      PATHS
        /usr/share/fonts/truetype/dejavu
        /usr/share/fonts/dejavu
        /System/Library/Fonts/Supplemental
        /Library/Fonts
    )
  else()
    unset(CHROMA_TEST_FONT CACHE)
  endif()
  if(CHROMA_TEST_FONT)
    add_test(NAME chroma_text_tests COMMAND chroma_text_tests ${CHROMA_TEST_FONT})
  else()
    add_test(NAME chroma_text_tests COMMAND chroma_text_tests)
  endif()
  if(CHROMA_HAS_FFMPEG AND CHROMA_FFMPEG_EXECUTABLE)
    set(CHROMA_MEDIA_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/color-segments.mkv)
    set(CHROMA_IMAGE_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/red.png)
    add_custom_command(
      OUTPUT ${CHROMA_MEDIA_FIXTURE} ${CHROMA_IMAGE_FIXTURE}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
      COMMAND ${CHROMA_FFMPEG_EXECUTABLE} -y -hide_banner -loglevel error
              -f lavfi -i color=c=blue:size=64x36:rate=30:duration=1
              -f lavfi -i color=c=red:size=64x36:rate=30:duration=1
              -filter_complex [0:v][1:v]concat=n=2:v=1:a=0
              -c:v ffv1 ${CHROMA_MEDIA_FIXTURE}
      COMMAND ${CHROMA_FFMPEG_EXECUTABLE} -y -hide_banner -loglevel error
              -f lavfi -i color=c=red:size=32x32:duration=1
              -frames:v 1 -threads 1 ${CHROMA_IMAGE_FIXTURE}
      VERBATIM
    )
    add_custom_target(chroma_media_fixture DEPENDS ${CHROMA_MEDIA_FIXTURE})
    set(CHROMA_VIDEO_COMPOSITION ${CMAKE_CURRENT_BINARY_DIR}/fixtures/video-composition.json)
    configure_file(tests/fixtures/video-composition.json.in ${CHROMA_VIDEO_COMPOSITION} @ONLY)
    add_dependencies(chroma_media_tests chroma_media_fixture)
    add_dependencies(chroma_render_tests chroma_media_fixture)
    add_test(NAME chroma_render_tests
             COMMAND chroma_render_tests ${CHROMA_MEDIA_FIXTURE} ${CHROMA_IMAGE_FIXTURE})
    add_test(NAME chroma_media_tests COMMAND chroma_media_tests ${CHROMA_MEDIA_FIXTURE})
    add_test(NAME chroma_cli_probes_media_fixture
             COMMAND chroma_cli probe ${CHROMA_MEDIA_FIXTURE})
    set_tests_properties(chroma_cli_probes_media_fixture PROPERTIES
      PASS_REGULAR_EXPRESSION "ffv1"
    )
    add_test(NAME chroma_cli_inspects_video_plan
             COMMAND chroma_cli inspect-plan ${CHROMA_VIDEO_COMPOSITION} --frame 30)
    set_tests_properties(chroma_cli_inspects_video_plan PROPERTIES
      PASS_REGULAR_EXPRESSION "composite_video_frame"
    )
    add_test(NAME chroma_cli_renders_video_frame
             COMMAND chroma_cli frame ${CHROMA_VIDEO_COMPOSITION} --frame 30
                     --output ${CMAKE_CURRENT_BINARY_DIR}/fixtures/rendered-video-frame.ppm)
    set(CHROMA_RENDERED_VIDEO ${CMAKE_CURRENT_BINARY_DIR}/fixtures/rendered-video.mp4)
    add_test(NAME chroma_cli_renders_video
             COMMAND chroma_cli render ${CHROMA_VIDEO_COMPOSITION}
                     --output ${CHROMA_RENDERED_VIDEO})
    add_test(NAME chroma_cli_probes_rendered_video
             COMMAND chroma_cli probe ${CHROMA_RENDERED_VIDEO})
    set_tests_properties(chroma_cli_probes_rendered_video PROPERTIES
      DEPENDS chroma_cli_renders_video
      PASS_REGULAR_EXPRESSION "h264"
    )
    add_test(NAME chroma_encoded_media_tests
             COMMAND chroma_media_tests ${CHROMA_RENDERED_VIDEO})
    set_tests_properties(chroma_encoded_media_tests PROPERTIES
      DEPENDS chroma_cli_renders_video
    )
    set(CHROMA_BENCHMARK_REPORT ${CMAKE_CURRENT_BINARY_DIR}/fixtures/benchmark.json)
    add_test(NAME chroma_cli_benchmarks_video
             COMMAND chroma_cli benchmark ${CHROMA_VIDEO_COMPOSITION}
                     --iterations 1 --output ${CHROMA_BENCHMARK_REPORT})
    add_test(NAME chroma_cli_benchmark_report
             COMMAND ${CMAKE_COMMAND} -E cat ${CHROMA_BENCHMARK_REPORT})
    set_tests_properties(chroma_cli_benchmark_report PROPERTIES
      DEPENDS chroma_cli_benchmarks_video
      PASS_REGULAR_EXPRESSION "frames_per_second"
    )
    if(CHROMA_TEST_FONT)
      set(CHROMA_BASIC_GRAPHICS_COMPOSITION
          ${CMAKE_CURRENT_BINARY_DIR}/fixtures/basic-graphics.json)
      configure_file(tests/fixtures/basic-graphics.json.in
                     ${CHROMA_BASIC_GRAPHICS_COMPOSITION} @ONLY)
      add_test(NAME chroma_cli_inspects_basic_graphics
               COMMAND chroma_cli inspect-optimized-plan
                       ${CHROMA_BASIC_GRAPHICS_COMPOSITION} --frame 0)
      set_tests_properties(chroma_cli_inspects_basic_graphics PROPERTIES
        PASS_REGULAR_EXPRESSION "prepared_resource_count[^0-9]*2"
      )
      add_test(NAME chroma_cli_renders_basic_graphics
               COMMAND chroma_cli frame ${CHROMA_BASIC_GRAPHICS_COMPOSITION} --frame 0
                       --output ${CMAKE_CURRENT_BINARY_DIR}/fixtures/basic-graphics.ppm)
      set(CHROMA_BASIC_GRAPHICS_BENCHMARK
          ${CMAKE_CURRENT_BINARY_DIR}/fixtures/basic-graphics-benchmark.json)
      add_test(NAME chroma_cli_benchmarks_basic_graphics
               COMMAND chroma_cli benchmark ${CHROMA_BASIC_GRAPHICS_COMPOSITION}
                       --iterations 1 --output ${CHROMA_BASIC_GRAPHICS_BENCHMARK})
      add_test(NAME chroma_cli_basic_graphics_benchmark_report
               COMMAND ${CMAKE_COMMAND} -E cat ${CHROMA_BASIC_GRAPHICS_BENCHMARK})
      set_tests_properties(chroma_cli_basic_graphics_benchmark_report PROPERTIES
        DEPENDS chroma_cli_benchmarks_basic_graphics
        PASS_REGULAR_EXPRESSION "frames_per_second"
      )
    endif()
  else()
    add_test(NAME chroma_render_tests COMMAND chroma_render_tests)
    add_test(NAME chroma_media_tests COMMAND chroma_media_tests)
  endif()

  add_test(
    NAME chroma_cli_validates_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/solid.json
  )

  add_test(
    NAME chroma_cli_validates_layered_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/layered.json
  )

  add_test(
    NAME chroma_cli_validates_video_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/video.json
  )

  add_test(
    NAME chroma_cli_validates_effects_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/effects.json
  )

  add_test(
    NAME chroma_cli_validates_image_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/image.json
  )

  add_test(
    NAME chroma_cli_validates_shapes_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/shapes.json
  )

  add_test(
    NAME chroma_cli_validates_text_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/text.json
  )

  add_test(
    NAME chroma_cli_inspects_example_plan
    COMMAND chroma_cli inspect-plan ${CMAKE_CURRENT_SOURCE_DIR}/examples/solid.json --frame 0
  )
  set_tests_properties(chroma_cli_inspects_example_plan PROPERTIES
    PASS_REGULAR_EXPRESSION "composite_solid"
  )

  add_test(
    NAME chroma_cli_inspects_optimized_plan
    COMMAND chroma_cli inspect-optimized-plan ${CMAKE_CURRENT_SOURCE_DIR}/examples/video.json --frame 0
  )
  set_tests_properties(chroma_cli_inspects_optimized_plan PROPERTIES
    PASS_REGULAR_EXPRESSION "fusion_candidate"
  )

  add_test(
    NAME chroma_cli_inspects_effect_rewrite
    COMMAND chroma_cli inspect-optimized-plan ${CMAKE_CURRENT_SOURCE_DIR}/examples/effects.json --frame 0
  )
  set_tests_properties(chroma_cli_inspects_effect_rewrite PROPERTIES
    PASS_REGULAR_EXPRESSION "rewrite_count[^0-9]*1"
  )
endif()
