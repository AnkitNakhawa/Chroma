cmake_minimum_required(VERSION 3.25)

project(
  chroma
  VERSION 0.1.0
  DESCRIPTION "Native render graph for agent-generated video"
  LANGUAGES C CXX
)

option(CHROMA_RELOCATABLE_INSTALL "Use only package-relative runtime search paths" OFF)
option(CHROMA_BUILD_TESTS "Build Chroma tests" ON)
option(CHROMA_REQUIRE_QUALITY_TESTS "Fail configuration unless the media quality gate is available" OFF)
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
      libswresample
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

add_library(chroma_core STATIC
  src/core/cache.cpp
  src/core/composition.cpp
  src/core/effect.cpp
  src/core/execution_plan.cpp
  src/core/preflight.cpp
  src/core/prepared_resource.cpp
  src/core/rational.cpp
  src/core/render.cpp
  src/core/timeline.cpp
  src/media/audio.cpp
  src/media/decode.cpp
  src/media/encode.cpp
  src/media/frame.cpp
  src/media/hardware.cpp
  src/media/media.cpp
  src/text/text.cpp
)
add_library(chroma::core ALIAS chroma_core)
set_target_properties(chroma_core PROPERTIES POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

add_library(chroma_c SHARED src/c_api/chroma.cpp)
add_library(chroma::c ALIAS chroma_c)
set_target_properties(chroma_c PROPERTIES OUTPUT_NAME chroma EXPORT_NAME c
  VERSION 1.0.0 SOVERSION 1 CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(NOT CHROMA_RELOCATABLE_INSTALL)
  set_target_properties(chroma_c PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()
if(APPLE)
  set_property(TARGET chroma_c APPEND PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
  set_property(TARGET chroma_c APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
endif()
target_compile_features(chroma_c PRIVATE cxx_std_20)
target_compile_definitions(chroma_c PRIVATE CHROMA_C_BUILD CHROMA_VERSION="${PROJECT_VERSION}")
target_include_directories(chroma_c PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(chroma_c PRIVATE chroma::core)
# Export only the explicitly declared C ABI, including when C++ runtime templates
# request default visibility. Regenerate this list when the public header changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/chroma/chroma.h)
file(STRINGS include/chroma/chroma.h CHROMA_API_DECLARATIONS REGEX "CHROMA_API.*CHROMA_CALL chroma_")
set(CHROMA_API_SYMBOLS "")
foreach(declaration IN LISTS CHROMA_API_DECLARATIONS)
  string(REGEX MATCH "CHROMA_CALL (chroma_[a-z0-9_]+)" match "${declaration}")
  list(APPEND CHROMA_API_SYMBOLS "${CMAKE_MATCH_1}")
endforeach()
if(APPLE)
  set(CHROMA_EXPORT_FILE ${CMAKE_CURRENT_BINARY_DIR}/chroma.exports)
  file(WRITE ${CHROMA_EXPORT_FILE} "")
  foreach(symbol IN LISTS CHROMA_API_SYMBOLS)
    file(APPEND ${CHROMA_EXPORT_FILE} "_${symbol}\n")
  endforeach()
  target_link_options(chroma_c PRIVATE "LINKER:-exported_symbols_list,${CHROMA_EXPORT_FILE}")
elseif(UNIX)
  set(CHROMA_EXPORT_FILE ${CMAKE_CURRENT_BINARY_DIR}/chroma.map)
  file(WRITE ${CHROMA_EXPORT_FILE} "CHROMA_1 { global:\n")
  foreach(symbol IN LISTS CHROMA_API_SYMBOLS)
    file(APPEND ${CHROMA_EXPORT_FILE} "${symbol};\n")
  endforeach()
  file(APPEND ${CHROMA_EXPORT_FILE} "local: *; };\n")
  target_link_options(chroma_c PRIVATE "LINKER:--version-script,${CHROMA_EXPORT_FILE}")
endif()
if(CHROMA_EXPORT_FILE)
  set_property(TARGET chroma_c APPEND PROPERTY LINK_DEPENDS ${CHROMA_EXPORT_FILE})
endif()



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

add_executable(chroma_c_example examples/c-api-render.c)
target_compile_features(chroma_c_example PRIVATE c_std_99)
set_target_properties(chroma_c_example PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(chroma_c_example PRIVATE chroma::c)

add_executable(chroma_cli src/cli/main.cpp)
set_target_properties(chroma_cli PROPERTIES OUTPUT_NAME chroma)
target_link_libraries(chroma_cli PRIVATE chroma::core)
if(NOT CHROMA_RELOCATABLE_INSTALL)
  set_target_properties(chroma_cli chroma_c_example PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()
if(APPLE)
  set_target_properties(chroma_cli PROPERTIES INSTALL_RPATH "@loader_path/../lib")
  set_target_properties(chroma_c_example PROPERTIES INSTALL_RPATH "@loader_path/../../lib")
elseif(UNIX)
  set_target_properties(chroma_cli PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
  set_target_properties(chroma_c_example PROPERTIES INSTALL_RPATH "$ORIGIN/../../lib")
endif()

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

  add_executable(chroma_preflight_tests tests/preflight_tests.cpp)
  target_link_libraries(chroma_preflight_tests PRIVATE chroma::core)
  add_test(NAME chroma_preflight_tests COMMAND chroma_preflight_tests)

  add_test(NAME chroma_cli_validates_animated_transforms
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/animated-transforms.json)
  add_test(NAME chroma_cli_inspects_animated_transforms
    COMMAND chroma_cli inspect-plan ${CMAKE_CURRENT_SOURCE_DIR}/examples/animated-transforms.json --frame 25)
  set_tests_properties(chroma_cli_inspects_animated_transforms PROPERTIES
    PASS_REGULAR_EXPRESSION "transform_2d")

  add_test(NAME chroma_cli_validates_audio_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/audio.json)

  find_package(Threads REQUIRED)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/c-api-control-test)
  add_executable(chroma_c_api_control_tests tests/c_api_control_tests.c)
  target_compile_features(chroma_c_api_control_tests PRIVATE c_std_99)
  set_target_properties(chroma_c_api_control_tests PROPERTIES LINKER_LANGUAGE C)
  target_link_libraries(chroma_c_api_control_tests PRIVATE chroma::c)
  add_test(NAME chroma_c_api_control_tests COMMAND chroma_c_api_control_tests ${CMAKE_CURRENT_BINARY_DIR}/c-api-control-test)
  add_executable(chroma_c_api_cancel_thread_tests tests/c_api_cancel_thread_tests.cpp)
  target_compile_features(chroma_c_api_cancel_thread_tests PRIVATE cxx_std_20)
  target_link_libraries(chroma_c_api_cancel_thread_tests PRIVATE chroma::c Threads::Threads)
  add_test(NAME chroma_c_api_cancel_thread_tests COMMAND chroma_c_api_cancel_thread_tests ${CMAKE_CURRENT_BINARY_DIR}/cancel-thread.mp4)
  set_tests_properties(chroma_c_api_control_tests chroma_c_api_cancel_thread_tests PROPERTIES TIMEOUT 30)

  add_executable(chroma_c_api_thread_tests tests/c_api_thread_tests.cpp)
  target_compile_features(chroma_c_api_thread_tests PRIVATE cxx_std_20)
  target_link_libraries(chroma_c_api_thread_tests PRIVATE chroma::c Threads::Threads)
  add_test(NAME chroma_c_api_thread_tests COMMAND chroma_c_api_thread_tests)

  add_executable(chroma_c_api_v1_tests tests/abi/v1_consumer.c)
  target_compile_features(chroma_c_api_v1_tests PRIVATE c_std_99)
  set_target_properties(chroma_c_api_v1_tests PROPERTIES LINKER_LANGUAGE C)
  target_link_libraries(chroma_c_api_v1_tests PRIVATE chroma::c)
  add_test(NAME chroma_c_api_v1_tests COMMAND chroma_c_api_v1_tests)
  if(UNIX)
    add_test(NAME chroma_c_api_symbols
      COMMAND ${CMAKE_COMMAND} -DCHROMA_LIBRARY=$<TARGET_FILE:chroma_c>
        -DCHROMA_NM=${CMAKE_NM} -DCHROMA_SYSTEM=${CMAKE_SYSTEM_NAME}
        -DCHROMA_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_c_abi.cmake)
  endif()
  add_test(NAME chroma_c_api_install_consumer
    COMMAND ${CMAKE_COMMAND} -DCHROMA_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DCHROMA_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DCHROMA_CONFIG=$<CONFIG>
      -DCHROMA_C_COMPILER=${CMAKE_C_COMPILER} -DCHROMA_SYSROOT=${CMAKE_OSX_SYSROOT}
      "-DCHROMA_LINK_FLAGS=${CMAKE_EXE_LINKER_FLAGS}" "-DCHROMA_C_FLAGS=${CMAKE_C_FLAGS}"
      "-DCHROMA_TEST_RUNTIME_DIRS=${CHROMA_FFMPEG_LIBRARY_DIRS};${CHROMA_TEXT_LIBRARY_DIRS}"
      -DCHROMA_EXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_consumer.cmake)
  set_tests_properties(chroma_c_api_install_consumer PROPERTIES TIMEOUT 120)

  add_executable(chroma_c_api_tests tests/c_api_tests.c)
  target_compile_features(chroma_c_api_tests PRIVATE c_std_99)
  set_target_properties(chroma_c_api_tests PROPERTIES LINKER_LANGUAGE C)
  target_link_libraries(chroma_c_api_tests PRIVATE chroma::c)
  add_test(NAME chroma_c_api_tests COMMAND chroma_c_api_tests ${CMAKE_CURRENT_BINARY_DIR}/c-api-test.mp4)

  add_executable(chroma_cache_tests tests/cache_tests.cpp)
  target_include_directories(chroma_cache_tests PRIVATE src/core)
  target_link_libraries(chroma_cache_tests PRIVATE chroma::core)
  add_test(NAME chroma_cache_tests COMMAND chroma_cache_tests)

  add_executable(chroma_render_control_tests tests/render_control_tests.cpp)
  target_link_libraries(chroma_render_control_tests PRIVATE chroma::core)
  add_test(NAME chroma_render_control_tests COMMAND chroma_render_control_tests ${CMAKE_CURRENT_BINARY_DIR}/render-control-test)
  set_tests_properties(chroma_render_control_tests PROPERTIES TIMEOUT 60)

  add_executable(chroma_audio_tests tests/audio_tests.cpp)
  target_link_libraries(chroma_audio_tests PRIVATE chroma::core)
  add_test(NAME chroma_audio_tests COMMAND chroma_audio_tests)

  add_executable(chroma_timeline_tests tests/timeline_tests.cpp)
  target_link_libraries(chroma_timeline_tests PRIVATE chroma::core)
  add_test(NAME chroma_timeline_tests COMMAND chroma_timeline_tests)

  add_executable(chroma_composition_tests tests/composition_tests.cpp)
  target_link_libraries(chroma_composition_tests PRIVATE chroma::core)
  add_test(NAME chroma_composition_tests COMMAND chroma_composition_tests)

  add_executable(chroma_graphics_equivalence benchmarks/graphics_equivalence.cpp)
  target_link_libraries(chroma_graphics_equivalence PRIVATE chroma::core nlohmann_json::nlohmann_json)

  add_executable(chroma_region_tests tests/region_tests.cpp)
  target_link_libraries(chroma_region_tests PRIVATE chroma::core)

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

  add_test(NAME chroma_cli_reports_hardware_capabilities COMMAND chroma_cli hardware)
  set_tests_properties(chroma_cli_reports_hardware_capabilities PROPERTIES
    PASS_REGULAR_EXPRESSION "\"pipeline_implemented\": false")
  if(CHROMA_TEST_FONT)
    add_test(NAME chroma_text_tests COMMAND chroma_text_tests ${CHROMA_TEST_FONT})
    add_test(NAME chroma_region_tests COMMAND chroma_region_tests ${CHROMA_TEST_FONT})
  else()
    add_test(NAME chroma_text_tests COMMAND chroma_text_tests)
    add_test(NAME chroma_region_tests COMMAND chroma_region_tests)
  endif()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)
    set(CHROMA_PREFLIGHT_ARGS --chroma $<TARGET_FILE:chroma_cli>)
    if(CHROMA_HAS_FFMPEG AND CHROMA_FFMPEG_EXECUTABLE)
      list(APPEND CHROMA_PREFLIGHT_ARGS --ffmpeg ${CHROMA_FFMPEG_EXECUTABLE})
    endif()
    if(CHROMA_TEST_FONT)
      list(APPEND CHROMA_PREFLIGHT_ARGS --font ${CHROMA_TEST_FONT})
    endif()
    add_test(NAME chroma_preflight_cli_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/preflight_cli_tests.py ${CHROMA_PREFLIGHT_ARGS})
    set_tests_properties(chroma_preflight_cli_tests PROPERTIES TIMEOUT 120)
    option(CHROMA_TEST_PYTHON_WRAPPER "Run ctypes integration tests (requires a compatible Python host)" ON)
    if(CHROMA_TEST_PYTHON_WRAPPER AND Python3_VERSION VERSION_GREATER_EQUAL "3.10")
      add_test(NAME chroma_python_wrapper_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/python_wrapper_tests.py
          --library $<TARGET_FILE:chroma_c>)
      set_tests_properties(chroma_python_wrapper_tests PROPERTIES TIMEOUT 90)
    endif()
    add_test(NAME chroma_release_packaging_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/release_packaging_tests.py)
    add_test(NAME chroma_showcase_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/showcase_tests.py
              $<TARGET_FILE:chroma_cli>)
    set_tests_properties(chroma_showcase_tests PROPERTIES TIMEOUT 120)
  endif()
  if(CHROMA_HAS_FFMPEG AND CHROMA_FFMPEG_EXECUTABLE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_program(CHROMA_FFPROBE_EXECUTABLE ffprobe REQUIRED)
    if(CHROMA_TEST_FONT)
      add_test(NAME chroma_region_cli_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/region_cli_tests.py
                $<TARGET_FILE:chroma_cli> ${CHROMA_TEST_FONT})
    else()
      add_test(NAME chroma_region_cli_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/region_cli_tests.py
                $<TARGET_FILE:chroma_cli>)
    endif()
    set_tests_properties(chroma_region_cli_tests PROPERTIES TIMEOUT 120)
    if(CHROMA_TEST_FONT)
      add_test(NAME chroma_quality_gate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/quality_gate.py
          --chroma $<TARGET_FILE:chroma_cli> --verifier $<TARGET_FILE:chroma_graphics_equivalence>
          --ffmpeg ${CHROMA_FFMPEG_EXECUTABLE} --ffprobe ${CHROMA_FFPROBE_EXECUTABLE} --font ${CHROMA_TEST_FONT}
          --output ${CMAKE_CURRENT_BINARY_DIR}/quality)
      set_tests_properties(chroma_quality_gate PROPERTIES TIMEOUT 600 LABELS quality)
    endif()


    add_test(NAME chroma_progress_cli_tests
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/progress_cli_tests.py
        $<TARGET_FILE:chroma_cli> ${CHROMA_FFPROBE_EXECUTABLE})
    set_tests_properties(chroma_progress_cli_tests PROPERTIES TIMEOUT 90)
    add_test(NAME chroma_c_api_integration
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/c_api_integration.py
              $<TARGET_FILE:chroma_c_example> ${CHROMA_FFMPEG_EXECUTABLE} ${CHROMA_FFPROBE_EXECUTABLE})
    set_tests_properties(chroma_c_api_integration PROPERTIES TIMEOUT 120)
    if(CHROMA_TEST_FONT)
      add_test(NAME chroma_cache_cli_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cache_integration.py
                $<TARGET_FILE:chroma_cli> ${CHROMA_FFMPEG_EXECUTABLE} ${CHROMA_TEST_FONT})
    else()
      add_test(NAME chroma_cache_cli_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cache_integration.py
                $<TARGET_FILE:chroma_cli> ${CHROMA_FFMPEG_EXECUTABLE})
    endif()
    set_tests_properties(chroma_cache_cli_integration PROPERTIES TIMEOUT 120)
    add_test(NAME chroma_animation_cli_integration
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/animation_integration.py
              $<TARGET_FILE:chroma_cli> ${CHROMA_FFMPEG_EXECUTABLE} ${CHROMA_FFPROBE_EXECUTABLE})
    set_tests_properties(chroma_animation_cli_integration PROPERTIES TIMEOUT 120)
    add_test(NAME chroma_audio_cli_integration
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_integration.py
              $<TARGET_FILE:chroma_cli> ${CHROMA_FFMPEG_EXECUTABLE} ${CHROMA_FFPROBE_EXECUTABLE})
    set_tests_properties(chroma_audio_cli_integration PROPERTIES TIMEOUT 120)
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

  add_test(NAME chroma_cli_validates_text_layout_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/text-layout.json)
  add_test(
    NAME chroma_cli_validates_text_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/text.json
  )

  add_test(
    NAME chroma_cli_validates_animated_opacity_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/animated-opacity.json
  )

  add_test(
    NAME chroma_cli_resolves_animated_opacity
    COMMAND chroma_cli inspect-plan
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/animated-opacity.json --frame 16
  )
  set_tests_properties(chroma_cli_resolves_animated_opacity PROPERTIES
    PASS_REGULAR_EXPRESSION "opacity[^0-9]*0.875"
  )

  add_test(
    NAME chroma_cli_validates_blur_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/blur.json
  )

  add_test(
    NAME chroma_cli_inspects_blur_barrier
    COMMAND chroma_cli inspect-optimized-plan
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/blur.json --frame 0
  )
  set_tests_properties(chroma_cli_inspects_blur_barrier PROPERTIES
    PASS_REGULAR_EXPRESSION "neighborhood_effect"
  )

  add_test(
    NAME chroma_cli_validates_mask_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/masks.json
  )

  add_test(
    NAME chroma_cli_inspects_mask_fusion
    COMMAND chroma_cli inspect-optimized-plan ${CMAKE_CURRENT_SOURCE_DIR}/examples/masks.json
            --frame 0
  )
  set_tests_properties(chroma_cli_inspects_mask_fusion PROPERTIES
    PASS_REGULAR_EXPRESSION "mask"
  )

  add_test(
    NAME chroma_cli_validates_crossfade_example
    COMMAND chroma_cli validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/crossfade.json
  )

  add_test(
    NAME chroma_cli_resolves_crossfade
    COMMAND chroma_cli inspect-plan ${CMAKE_CURRENT_SOURCE_DIR}/examples/crossfade.json
            --frame 30
  )
  set_tests_properties(chroma_cli_resolves_crossfade PROPERTIES
    PASS_REGULAR_EXPRESSION "opacity[^0-9]*0.5"
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

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(CHROMA_PACKAGE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/Chroma)
install(TARGETS chroma_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT CLI)
install(TARGETS chroma_c_example RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/chroma COMPONENT Examples)
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/chroma COMPONENT Documentation)
install(FILES docs/c-api.md docs/render-control.md docs/python.md docs/composition-format.md docs/preflight.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/chroma/docs COMPONENT Documentation)
install(FILES examples/solid.json examples/animated-transforms.json examples/text-layout.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/chroma/examples COMPONENT Examples)
install(TARGETS chroma_c EXPORT ChromaTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT CAPI
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT CAPI
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT CAPI)
install(FILES include/chroma/chroma.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/chroma COMPONENT CAPI)
install(FILES examples/c-api-render.c DESTINATION ${CMAKE_INSTALL_DATADIR}/chroma/examples COMPONENT CAPI)
install(EXPORT ChromaTargets NAMESPACE chroma:: DESTINATION ${CHROMA_PACKAGE_DIR} COMPONENT CAPI)
configure_package_config_file(cmake/ChromaConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/ChromaConfig.cmake
  INSTALL_DESTINATION ${CHROMA_PACKAGE_DIR})
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/ChromaConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ChromaConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/ChromaConfigVersion.cmake
        DESTINATION ${CHROMA_PACKAGE_DIR} COMPONENT CAPI)

# CI requests this explicitly: missing optional dependencies must not silently
# turn a promised quality gate into a green job with zero tests.
if(CHROMA_REQUIRE_QUALITY_TESTS)
  if(NOT TEST chroma_quality_gate)
    message(FATAL_ERROR "Quality gate unavailable: enable tests and provide FFmpeg/ffprobe, Python, HarfBuzz/FreeType and a test font")
  endif()
endif()

install(DIRECTORY python/src/chroma_render DESTINATION ${CMAKE_INSTALL_DATADIR}/chroma/python
  COMPONENT Python FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
install(FILES examples/python-render.py DESTINATION ${CMAKE_INSTALL_DATADIR}/chroma/examples COMPONENT Python)
