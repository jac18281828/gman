# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Driver for the "install" test (see tests/CMakeLists.txt for the arguments
# and tests/install/ for the consumer project step 3 builds). Run with
# cmake -P; each step's execute_process stops the script at the first
# failure, so the steps run strictly in order.

foreach(var
    GMAN_BUILD_DIR
    GMAN_SOURCE_DIR
    GMAN_PREFIX
    GMAN_RENDER_DIR
    GMAN_CONSUMER_SOURCE_DIR
    GMAN_CONSUMER_BUILD_DIR
    GMAN_BINDIR
    GMAN_LIBDIR
    GMAN_VERSION
    GMAN_C_COMPILER
    GMAN_CXX_COMPILER
    GMAN_BUILD_TYPE)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "install_test.cmake: ${var} is required")
  endif()
endforeach()

# --- Step 1: install into a fresh prefix ------------------------------------

file(REMOVE_RECURSE "${GMAN_PREFIX}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${GMAN_BUILD_DIR}" --prefix "${GMAN_PREFIX}"
  RESULT_VARIABLE GMAN_STEP1_RESULT)
if(NOT GMAN_STEP1_RESULT EQUAL 0)
  message(FATAL_ERROR "step 1 (install): cmake --install exited ${GMAN_STEP1_RESULT}")
endif()

# --- Step 2: render every shipped shader from the installed bin/gman -------
#
# No LD_LIBRARY_PATH/DYLD_LIBRARY_PATH is set here or on the "install" test
# itself: the build-tree loader path the other tests set would hand the
# installed gman the build tree's plugins on Linux, silently passing this
# step for the wrong reason. Only the install rpath may resolve them.

file(REMOVE_RECURSE "${GMAN_RENDER_DIR}")
file(MAKE_DIRECTORY "${GMAN_RENDER_DIR}")

set(GMAN_INSTALLED_EXE "${GMAN_PREFIX}/${GMAN_BINDIR}/gman")
if(NOT EXISTS "${GMAN_INSTALLED_EXE}")
  message(FATAL_ERROR "step 2 (render): ${GMAN_INSTALLED_EXE} does not exist")
endif()

function(gman_render_shader rib_name image_name)
  execute_process(
    COMMAND "${GMAN_INSTALLED_EXE}" "${GMAN_SOURCE_DIR}/tests/rib/${rib_name}"
    WORKING_DIRECTORY "${GMAN_RENDER_DIR}"
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "step 2 (render): rendering ${rib_name} exited ${result}")
  endif()

  set(image "${GMAN_RENDER_DIR}/${image_name}")
  if(NOT EXISTS "${image}")
    message(FATAL_ERROR "step 2 (render): ${rib_name} did not write ${image_name}")
  endif()
  file(SIZE "${image}" size)
  if(size EQUAL 0)
    message(FATAL_ERROR "step 2 (render): ${image_name} is empty")
  endif()
endfunction()

# matte, plastic, metal.
gman_render_shader(shaders.rib shaders.tif)
# paintedplastic; warns "cannot open, using opaque black" for the texture
# texture_test alone writes, and still exits 0.
gman_render_shader(texture.rib texture.tif)
# shinymetal.
gman_render_shader(shinymetal_degrades.rib shinymetal_degrades.tif)

# --- Step 3: configure, build and run a consumer against the prefix --------

file(REMOVE_RECURSE "${GMAN_CONSUMER_BUILD_DIR}")

set(GMAN_STEP3_CONFIGURE_ARGS
  -S "${GMAN_CONSUMER_SOURCE_DIR}"
  -B "${GMAN_CONSUMER_BUILD_DIR}"
  -D "CMAKE_PREFIX_PATH=${GMAN_PREFIX}"
  -D "CMAKE_C_COMPILER=${GMAN_C_COMPILER}"
  -D "CMAKE_CXX_COMPILER=${GMAN_CXX_COMPILER}"
  -D "CMAKE_BUILD_TYPE=${GMAN_BUILD_TYPE}"
  -D "CMAKE_CXX_FLAGS=${GMAN_CXX_FLAGS}"
  -D "CMAKE_EXE_LINKER_FLAGS=${GMAN_EXE_LINKER_FLAGS}"
  -D "GMAN_CONSUMER_VERSION=${GMAN_VERSION}")
if(GMAN_GENERATOR)
  list(PREPEND GMAN_STEP3_CONFIGURE_ARGS -G "${GMAN_GENERATOR}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" ${GMAN_STEP3_CONFIGURE_ARGS}
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "step 3 (consumer): configure exited ${result}")
endif()

# A system gman on the loader's own default search path must not stand in
# for this prefix: the consumer has to have found the package here.
load_cache("${GMAN_CONSUMER_BUILD_DIR}" READ_WITH_PREFIX GMAN_STEP3_ gman_DIR)
if(NOT DEFINED GMAN_STEP3_gman_DIR)
  message(FATAL_ERROR "step 3 (consumer): gman_DIR is not in the consumer's cache")
endif()
get_filename_component(GMAN_STEP3_ACTUAL_DIR "${GMAN_STEP3_gman_DIR}" ABSOLUTE)
get_filename_component(GMAN_STEP3_EXPECTED_DIR
  "${GMAN_PREFIX}/${GMAN_LIBDIR}/cmake/gman" ABSOLUTE)
if(NOT GMAN_STEP3_ACTUAL_DIR STREQUAL GMAN_STEP3_EXPECTED_DIR)
  message(FATAL_ERROR
    "step 3 (consumer): gman_DIR is ${GMAN_STEP3_ACTUAL_DIR}, expected ${GMAN_STEP3_EXPECTED_DIR}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${GMAN_CONSUMER_BUILD_DIR}"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "step 3 (consumer): build exited ${result}")
endif()

set(GMAN_CONSUMER_EXE "${GMAN_CONSUMER_BUILD_DIR}/consumer")
if(NOT EXISTS "${GMAN_CONSUMER_EXE}")
  message(FATAL_ERROR "step 3 (consumer): ${GMAN_CONSUMER_EXE} was not built")
endif()

execute_process(
  COMMAND "${GMAN_CONSUMER_EXE}"
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "step 3 (consumer): running consumer exited ${result}")
endif()
