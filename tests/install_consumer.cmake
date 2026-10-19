cmake_minimum_required(VERSION 3.25)
# Install, relocate, and consume with a project that enables only the C language.
set(root "${CHROMA_BINARY_DIR}/c-api-install-test")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${CHROMA_BINARY_DIR}"
  --prefix "${root}/original" --component CAPI --config "${CHROMA_CONFIG}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "install failed: ${output}\n${error}")
endif()
file(RENAME "${root}/original" "${root}/relocated")
# Raw CAPI installation is an SDK component; portable packaging supplies its
# runtime closure separately. Include that closure in this export/consumer test.
foreach(directory IN LISTS CHROMA_TEST_RUNTIME_DIRS)
  if(NOT directory STREQUAL "")
    file(GLOB libraries "${directory}/*.dylib" "${directory}/*.so*")
    foreach(library IN LISTS libraries)
      file(COPY "${library}" DESTINATION "${root}/relocated/lib" FOLLOW_SYMLINK_CHAIN)
    endforeach()
  endif()
endforeach()
set(extra "")
if(CHROMA_SYSROOT)
  list(APPEND extra "-DCMAKE_OSX_SYSROOT=${CHROMA_SYSROOT}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CHROMA_SOURCE_DIR}/tests/install" -B "${root}/consumer"
  "-DCMAKE_PREFIX_PATH=${root}/relocated" "-DCMAKE_C_COMPILER=${CHROMA_C_COMPILER}"
  "-DCMAKE_BUILD_TYPE=${CHROMA_CONFIG}" "-DCMAKE_EXE_LINKER_FLAGS=${CHROMA_LINK_FLAGS}"
  "-DCMAKE_C_FLAGS=${CHROMA_C_FLAGS}" ${extra}
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C-only configure failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${root}/consumer" --config "${CHROMA_CONFIG}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C-only build failed: ${output}\n${error}")
endif()
set(executable "${root}/consumer/consumer${CHROMA_EXE_SUFFIX}")
if(NOT EXISTS "${executable}")
  set(executable "${root}/consumer/${CHROMA_CONFIG}/consumer${CHROMA_EXE_SUFFIX}")
endif()
execute_process(COMMAND "${executable}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "C-only execution failed: ${output}\n${error}")
endif()
