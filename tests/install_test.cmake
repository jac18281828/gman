# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Driver for the "install" test (see tests/CMakeLists.txt for the arguments
# and tests/install/ for the consumer project step 3 builds). Run with
# cmake -P; gman_run_or_fail's FATAL_ERROR stops the script at the first
# failing step, so the steps run strictly in order.

# cmake -P sets no policies; this one makes if(IN_LIST) available on CMake 3.
cmake_minimum_required(VERSION 3.21)

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
    GMAN_MODULE_LINKER_FLAGS
    GMAN_HAS_RAYTRACER
    GMAN_HAS_PATHTRACER
    GMAN_HAS_RADIOSITY
    GMAN_INSTALLED_HEADERS_MANIFEST
    GMAN_IMAGECHECK)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "install_test.cmake: ${var} is required")
  endif()
endforeach()

# The size, in pixels, of tests/rib/smoke.rib and of the image consumer_all
# writes.
set(GMAN_SMOKE_WIDTH 40)
set(GMAN_SMOKE_HEIGHT 30)

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

# Stops the script unless GMAN_IMAGECHECK accepts image as a TIFF of width by
# height; with LIT it also wants a pixel with a nonzero colour channel. The
# checker's one-line report reaches the test's output.
function(gman_check_image label image width height)
  cmake_parse_arguments(GMAN_IMAGE "LIT" "" "" ${ARGN})
  set(checker_arguments "${image}" "${width}" "${height}")
  if(GMAN_IMAGE_LIT)
    list(APPEND checker_arguments lit)
  endif()
  gman_run_or_fail("${label}: checking ${image}"
    COMMAND "${GMAN_IMAGECHECK}" ${checker_arguments})
endfunction()

# Appends to the variable named by result_variable one line per item of
# actual_items missing from expected_items, signed "+" and ending extra_reason,
# and one per item of expected_items missing from actual_items, signed "-" and
# ending missing_reason. Both item arguments name list variables.
function(gman_append_signed_differences result_variable actual_items expected_items extra_reason missing_reason)
  set(differences "${${result_variable}}")
  foreach(item IN LISTS ${actual_items})
    if(NOT item IN_LIST ${expected_items})
      string(APPEND differences "\n  +${item} ${extra_reason}")
    endif()
  endforeach()
  foreach(item IN LISTS ${expected_items})
    if(NOT item IN_LIST ${actual_items})
      string(APPEND differences "\n  -${item} ${missing_reason}")
    endif()
  endforeach()
  set(${result_variable} "${differences}" PARENT_SCOPE)
endfunction()

# Stops the script unless the consumer executable name was built in the
# consumer's build directory, then runs it, with an optional WORKING_DIRECTORY.
function(gman_run_consumer name)
  set(consumer_exe "${GMAN_CONSUMER_BUILD_DIR}/${name}")
  if(NOT EXISTS "${consumer_exe}")
    message(FATAL_ERROR "step 3 (consumer): ${consumer_exe} was not built")
  endif()
  cmake_parse_arguments(GMAN_CONSUMER "" "WORKING_DIRECTORY" "" ${ARGN})
  if(GMAN_CONSUMER_WORKING_DIRECTORY)
    gman_run_or_fail("step 3 (consumer): running ${name}"
      COMMAND "${consumer_exe}"
      WORKING_DIRECTORY "${GMAN_CONSUMER_WORKING_DIRECTORY}")
  else()
    gman_run_or_fail("step 3 (consumer): running ${name}"
      COMMAND "${consumer_exe}")
  endif()
endfunction()

# --- Step 1: install into a fresh prefix ------------------------------------

file(REMOVE_RECURSE "${GMAN_PREFIX}")
gman_run_or_fail("step 1 (install): cmake --install"
  COMMAND "${CMAKE_COMMAND}" --install "${GMAN_BUILD_DIR}" --prefix "${GMAN_PREFIX}")

# The headers directly under include/gman are exactly the ones the manifest
# lists. A header that appears or disappears is a change to the installed
# interface, so the difference fails the step, each name carrying its sign:
# "+" installed and not listed, "-" listed and not installed.
file(GLOB GMAN_INSTALLED_HEADERS LIST_DIRECTORIES false
  RELATIVE "${GMAN_PREFIX}/include/gman"
  "${GMAN_PREFIX}/include/gman/*")
file(STRINGS "${GMAN_INSTALLED_HEADERS_MANIFEST}" GMAN_LISTED_HEADERS REGEX ".")

set(GMAN_HEADER_DIFFERENCES "")
gman_append_signed_differences(GMAN_HEADER_DIFFERENCES
  GMAN_INSTALLED_HEADERS GMAN_LISTED_HEADERS
  "is installed and not listed" "is listed and not installed")
if(GMAN_HEADER_DIFFERENCES)
  message(FATAL_ERROR
    "step 1 (install): include/gman differs from ${GMAN_INSTALLED_HEADERS_MANIFEST}:${GMAN_HEADER_DIFFERENCES}")
endif()

# The core library installs as the one file libgman.*: the "." that follows
# libgman leaves libgman_ray, libgman_raster and the plugins out.
file(GLOB GMAN_INSTALLED_CORE_LIBRARIES LIST_DIRECTORIES false
  "${GMAN_PREFIX}/${GMAN_LIBDIR}/libgman.*")
list(LENGTH GMAN_INSTALLED_CORE_LIBRARIES GMAN_INSTALLED_CORE_LIBRARY_COUNT)
if(NOT GMAN_INSTALLED_CORE_LIBRARY_COUNT EQUAL 1)
  message(FATAL_ERROR
    "step 1 (install): ${GMAN_PREFIX}/${GMAN_LIBDIR} holds ${GMAN_INSTALLED_CORE_LIBRARY_COUNT} "
    "files matching libgman.*, expected the core library alone: ${GMAN_INSTALLED_CORE_LIBRARIES}")
endif()

# --- Step 2: render from the installed bin/gman: shaders, then renderers ---
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

function(gman_render_shader rib_name image_name width height)
  gman_run_or_fail("step 2 (render): rendering ${rib_name}"
    COMMAND "${GMAN_INSTALLED_EXE}" "${GMAN_SOURCE_DIR}/tests/rib/${rib_name}"
    WORKING_DIRECTORY "${GMAN_RENDER_DIR}")
  gman_check_image("step 2 (render): ${rib_name}" "${GMAN_RENDER_DIR}/${image_name}" ${width} ${height})
endfunction()

# matte, plastic, metal.
gman_render_shader(shaders.rib shaders.tif 600 200)
# paintedplastic; warns "cannot open, using opaque black" for the texture
# texture_test alone writes, and still exits 0.
gman_render_shader(texture.rib texture.tif 200 200)
# shinymetal.
gman_render_shader(shinymetal_degrades.rib shinymetal_degrades.tif 200 200)

# R4: `-r gmanraytracer` from an install prefix, on the one fixture step 2
# above does not already render. Only when the plugin was built -- GMAN_HAS_RAYTRACER
# reflects GMAN_BUILD_RAYTRACER, passed in by tests/CMakeLists.txt.
if(GMAN_HAS_RAYTRACER)
  gman_run_or_fail("step 2 (render): rendering r4_raytracer.rib under -r gmanraytracer"
    COMMAND "${GMAN_INSTALLED_EXE}" -r gmanraytracer "${GMAN_SOURCE_DIR}/tests/rib/r4_raytracer.rib"
    WORKING_DIRECTORY "${GMAN_RENDER_DIR}")
  gman_check_image("step 2 (render): r4_raytracer.rib" "${GMAN_RENDER_DIR}/r4_raytracer.tif" 640 480)
endif()

# Renders tests/rib/<rib_name> under -r <renderer> from the prefix and checks
# the exit status, then that <image_name> is a TIFF of width by height. With
# CHECK_PASS_WARNING, a render whose output carries gman::loadIndirectPass's
# warning prefix fails too: a missing or unloadable pass costs the indirect
# light only, so an image proves nothing about whether the pass loaded.
function(gman_render_ray_scene rib_name image_name width height renderer)
  cmake_parse_arguments(GMAN_SCENE "CHECK_PASS_WARNING" "" "" ${ARGN})
  execute_process(
    COMMAND "${GMAN_INSTALLED_EXE}" -r "${renderer}" "${GMAN_SOURCE_DIR}/tests/rib/${rib_name}"
    WORKING_DIRECTORY "${GMAN_RENDER_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "step 2 (render): rendering ${rib_name} under -r ${renderer} exited ${result}\n${output}${errors}")
  endif()

  if(GMAN_SCENE_CHECK_PASS_WARNING)
    string(FIND "${output}${errors}" "Indirect-light pass \"" warning_at)
    if(NOT warning_at EQUAL -1)
      message(FATAL_ERROR
        "step 2 (render): ${rib_name} under -r ${renderer} warned that its indirect-light pass did not load\n${output}${errors}")
    endif()
  endif()

  gman_check_image("step 2 (render): ${rib_name} under -r ${renderer}"
    "${GMAN_RENDER_DIR}/${image_name}" ${width} ${height})
endfunction()

# The path tracer from an install prefix. pathtracer_samples.rib names no
# indirect pass. Only when the plugin was built -- GMAN_HAS_PATHTRACER
# reflects GMAN_BUILD_PATHTRACER.
if(GMAN_HAS_PATHTRACER)
  gman_render_ray_scene(pathtracer_samples.rib pathtracer_samples.tif 32 32 gmanpathtracer)
endif()

# The radiosity pass from an install prefix: the ray tracer hosts it, and
# radiosity_box.rib selects it with Option "render" "string indirect".
# GMAN_HAS_RADIOSITY reflects GMAN_BUILD_RADIOSITY.
if(GMAN_HAS_RADIOSITY AND GMAN_HAS_RAYTRACER)
  gman_render_ray_scene(radiosity_box.rib radiosity_box.tif 24 24 gmanraytracer CHECK_PASS_WARNING)
endif()

# One scene under every installed renderer: tests/rib/smoke.rib renders at
# GMAN_SMOKE_WIDTH by GMAN_SMOKE_HEIGHT in a directory of its own, and the
# image carries a lit pixel. A shader that fails to load stops the file, so
# each row also proves matte and plastic load from the prefix.
function(gman_render_smoke renderer)
  set(smoke_dir "${GMAN_RENDER_DIR}/smoke-${renderer}")
  file(MAKE_DIRECTORY "${smoke_dir}")
  gman_run_or_fail("step 2 (smoke): rendering smoke.rib under -r ${renderer}"
    COMMAND "${GMAN_INSTALLED_EXE}" -r "${renderer}" "${GMAN_SOURCE_DIR}/tests/rib/smoke.rib"
    WORKING_DIRECTORY "${smoke_dir}")
  gman_check_image("step 2 (smoke): smoke.rib under -r ${renderer}"
    "${smoke_dir}/smoke.tif" ${GMAN_SMOKE_WIDTH} ${GMAN_SMOKE_HEIGHT} LIT)
endfunction()

set(GMAN_SMOKE_RENDERERS gmanzbuffer)
if(GMAN_HAS_RAYTRACER)
  list(APPEND GMAN_SMOKE_RENDERERS gmanraytracer)
endif()
if(GMAN_HAS_PATHTRACER)
  list(APPEND GMAN_SMOKE_RENDERERS gmanpathtracer)
endif()
foreach(renderer IN LISTS GMAN_SMOKE_RENDERERS)
  gman_render_smoke(${renderer})
endforeach()

# The prefix installs exactly the renderers the smoke rows render. A
# renderer's file is libgman and a lower-case letter, then the rest
# (libgmanzbuffer.so, libgmanraytracer.so, libgmanpathtracer.so); the RIB
# writer, libgmanrib.so, is the one other file of that form and no renderer.
# A renderer named otherwise escapes this check. A difference fails the step,
# each name carrying its sign: "+" installed and no row renders it, "-" a row
# names it and it is not installed.
file(GLOB GMAN_INSTALLED_RENDERER_FILES LIST_DIRECTORIES false
  RELATIVE "${GMAN_PREFIX}/${GMAN_LIBDIR}"
  "${GMAN_PREFIX}/${GMAN_LIBDIR}/libgman[a-z]*.so")
list(REMOVE_ITEM GMAN_INSTALLED_RENDERER_FILES libgmanrib.so)

set(GMAN_SMOKE_RENDERER_FILES "")
foreach(renderer IN LISTS GMAN_SMOKE_RENDERERS)
  list(APPEND GMAN_SMOKE_RENDERER_FILES "lib${renderer}.so")
endforeach()

set(GMAN_RENDERER_DIFFERENCES "")
gman_append_signed_differences(GMAN_RENDERER_DIFFERENCES
  GMAN_INSTALLED_RENDERER_FILES GMAN_SMOKE_RENDERER_FILES
  "is installed and no smoke row renders it" "has a smoke row and is not installed")
if(GMAN_RENDERER_DIFFERENCES)
  message(FATAL_ERROR
    "step 2 (smoke): the installed renderers differ from the smoke rows:${GMAN_RENDERER_DIFFERENCES}\n"
    "A renderer's file is libgman followed by a lower-case letter, as in libgmanzbuffer.so; "
    "this check finds renderers by that name, so a renderer named otherwise escapes it.")
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
          -D "CMAKE_MODULE_LINKER_FLAGS=${GMAN_MODULE_LINKER_FLAGS}"
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

gman_run_consumer(consumer)
gman_run_consumer(consumer_ray)
gman_run_consumer(consumer_raster)

set(GMAN_CONSUMER_ALL_DIR "${GMAN_RENDER_DIR}/consumer_all")
file(MAKE_DIRECTORY "${GMAN_CONSUMER_ALL_DIR}")
gman_run_consumer(consumer_all WORKING_DIRECTORY "${GMAN_CONSUMER_ALL_DIR}")
gman_check_image("step 3 (consumer): consumer_all"
  "${GMAN_CONSUMER_ALL_DIR}/consumer_all.tif" ${GMAN_SMOKE_WIDTH} ${GMAN_SMOKE_HEIGHT} LIT)

foreach(plugin shader_plugin renderer_plugin)
  set(GMAN_PLUGIN_FILE "${GMAN_CONSUMER_BUILD_DIR}/lib${plugin}.so")
  if(NOT EXISTS "${GMAN_PLUGIN_FILE}")
    message(FATAL_ERROR "step 3 (consumer): ${GMAN_PLUGIN_FILE} was not built")
  endif()
endforeach()
