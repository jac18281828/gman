# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Driver for the "install" test (see tests/CMakeLists.txt for the arguments
# and tests/install/ for the consumer project step 3 builds). Run with
# cmake -P; gman_run_or_fail's FATAL_ERROR stops the script at the first
# failing step, so the steps run strictly in order.

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
    GMAN_GENERATOR
    GMAN_C_COMPILER
    GMAN_CXX_COMPILER
    GMAN_BUILD_TYPE
    GMAN_CXX_FLAGS
    GMAN_EXE_LINKER_FLAGS
    GMAN_HAS_RAYTRACER)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "install_test.cmake: ${var} is required")
  endif()
endforeach()

# Runs one COMMAND, with an optional WORKING_DIRECTORY, and stops the whole
# script with a labelled FATAL_ERROR naming the exit code on nonzero.
function(gman_run_or_fail label)
  cmake_parse_arguments(GMAN_RUN "" "WORKING_DIRECTORY" "COMMAND" ${ARGN})
  if(GMAN_RUN_WORKING_DIRECTORY)
    execute_process(
      COMMAND ${GMAN_RUN_COMMAND}
      WORKING_DIRECTORY "${GMAN_RUN_WORKING_DIRECTORY}"
      RESULT_VARIABLE GMAN_RUN_RESULT)
  else()
    execute_process(
      COMMAND ${GMAN_RUN_COMMAND}
      RESULT_VARIABLE GMAN_RUN_RESULT)
  endif()
  if(NOT GMAN_RUN_RESULT EQUAL 0)
    message(FATAL_ERROR "${label} exited ${GMAN_RUN_RESULT}")
  endif()
endfunction()

# --- Step 1: install into a fresh prefix ------------------------------------

file(REMOVE_RECURSE "${GMAN_PREFIX}")
gman_run_or_fail("step 1 (install): cmake --install"
  COMMAND "${CMAKE_COMMAND}" --install "${GMAN_BUILD_DIR}" --prefix "${GMAN_PREFIX}")

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
  gman_run_or_fail("step 2 (render): rendering ${rib_name}"
    COMMAND "${GMAN_INSTALLED_EXE}" "${GMAN_SOURCE_DIR}/tests/rib/${rib_name}"
    WORKING_DIRECTORY "${GMAN_RENDER_DIR}")

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

# R4: `-r gmanraytracer` from an install prefix, on the one fixture step 2
# above does not already render. Only when the plugin was built -- GMAN_HAS_RAYTRACER
# reflects GMAN_BUILD_RAYTRACER, passed in by tests/CMakeLists.txt.
if(GMAN_HAS_RAYTRACER)
  gman_run_or_fail("step 2 (render): rendering r4_raytracer.rib under -r gmanraytracer"
    COMMAND "${GMAN_INSTALLED_EXE}" -r gmanraytracer "${GMAN_SOURCE_DIR}/tests/rib/r4_raytracer.rib"
    WORKING_DIRECTORY "${GMAN_RENDER_DIR}")

  set(GMAN_RAYTRACER_IMAGE "${GMAN_RENDER_DIR}/r4_raytracer.tif")
  if(NOT EXISTS "${GMAN_RAYTRACER_IMAGE}")
    message(FATAL_ERROR "step 2 (render): r4_raytracer.rib did not write r4_raytracer.tif")
  endif()
  file(SIZE "${GMAN_RAYTRACER_IMAGE}" GMAN_RAYTRACER_IMAGE_SIZE)
  if(GMAN_RAYTRACER_IMAGE_SIZE EQUAL 0)
    message(FATAL_ERROR "step 2 (render): r4_raytracer.tif is empty")
  endif()
endif()

# --- Step 3: configure, build and run a consumer against the prefix --------

file(REMOVE_RECURSE "${GMAN_CONSUMER_BUILD_DIR}")

gman_run_or_fail("step 3 (consumer): configure"
  COMMAND "${CMAKE_COMMAND}"
          -S "${GMAN_CONSUMER_SOURCE_DIR}"
          -B "${GMAN_CONSUMER_BUILD_DIR}"
          -G "${GMAN_GENERATOR}"
          -D "CMAKE_PREFIX_PATH=${GMAN_PREFIX}"
          -D "CMAKE_C_COMPILER=${GMAN_C_COMPILER}"
          -D "CMAKE_CXX_COMPILER=${GMAN_CXX_COMPILER}"
          -D "CMAKE_BUILD_TYPE=${GMAN_BUILD_TYPE}"
          -D "CMAKE_CXX_FLAGS=${GMAN_CXX_FLAGS}"
          -D "CMAKE_EXE_LINKER_FLAGS=${GMAN_EXE_LINKER_FLAGS}"
          -D "GMAN_CONSUMER_VERSION=${GMAN_VERSION}")

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

gman_run_or_fail("step 3 (consumer): build"
  COMMAND "${CMAKE_COMMAND}" --build "${GMAN_CONSUMER_BUILD_DIR}")

set(GMAN_CONSUMER_EXE "${GMAN_CONSUMER_BUILD_DIR}/consumer")
if(NOT EXISTS "${GMAN_CONSUMER_EXE}")
  message(FATAL_ERROR "step 3 (consumer): ${GMAN_CONSUMER_EXE} was not built")
endif()

gman_run_or_fail("step 3 (consumer): running consumer"
  COMMAND "${GMAN_CONSUMER_EXE}")
