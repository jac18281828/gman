# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Driver for the "installscript" test (see tests/CMakeLists.txt for the
# arguments). Stages a release tarball tree as release.yml does, then drives
# packaging/install.sh through install, render, repeat install, uninstall and
# a refused flag. Run with cmake -P; FATAL_ERROR stops the script at the first
# failing step, so the steps run strictly in order.

# cmake -P sets no policies; this one makes if(IN_LIST) available on CMake 3.
cmake_minimum_required(VERSION 3.21)

foreach(var
    GMAN_BUILD_DIR
    GMAN_SOURCE_DIR
    GMAN_WORK_DIR
    GMAN_HAS_RAYTRACER)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "installscript_test.cmake: ${var} is required")
  endif()
endforeach()

set(GMAN_STAGE "${GMAN_WORK_DIR}/stage/gman-test")
set(GMAN_PREFIX "${GMAN_WORK_DIR}/prefix")
set(GMAN_RENDER_DIR "${GMAN_WORK_DIR}/render")
set(GMAN_MANIFEST "${GMAN_PREFIX}/share/gman/install_manifest.txt")
set(GMAN_INSTALLER "${GMAN_STAGE}/install.sh")

# Runs COMMAND and stores its exit code in the caller's <result>. Standard
# output and error land in <result>_OUT and <result>_ERR.
function(gman_run result)
  cmake_parse_arguments(GMAN_RUN "" "WORKING_DIRECTORY" "COMMAND" ${ARGN})
  if(NOT GMAN_RUN_WORKING_DIRECTORY)
    set(GMAN_RUN_WORKING_DIRECTORY "${GMAN_WORK_DIR}")
  endif()
  execute_process(
    COMMAND ${GMAN_RUN_COMMAND}
    WORKING_DIRECTORY "${GMAN_RUN_WORKING_DIRECTORY}"
    RESULT_VARIABLE code
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
  set(${result} "${code}" PARENT_SCOPE)
  set(${result}_OUT "${out}" PARENT_SCOPE)
  set(${result}_ERR "${err}" PARENT_SCOPE)
endfunction()

function(gman_expect_exit label expected)
  gman_run(code ${ARGN})
  if(NOT code EQUAL expected)
    message(FATAL_ERROR
      "${label} exited ${code}, expected ${expected}\n${code_OUT}${code_ERR}")
  endif()
endfunction()

# The paths under the prefix, relative to it, files and directories both.
function(gman_list_prefix result)
  file(GLOB_RECURSE found LIST_DIRECTORIES true RELATIVE "${GMAN_PREFIX}"
    "${GMAN_PREFIX}/*")
  list(SORT found)
  set(${result} "${found}" PARENT_SCOPE)
endfunction()

# Splits the manifest into <files> and <dirs>, each a list of paths relative
# to the prefix; a trailing slash marks a directory.
function(gman_read_manifest files dirs)
  file(STRINGS "${GMAN_MANIFEST}" lines)
  set(listed_files "")
  set(listed_dirs "")
  foreach(line IN LISTS lines)
    string(FIND "${line}" "${GMAN_PREFIX}/" at)
    if(NOT at EQUAL 0)
      message(FATAL_ERROR "manifest entry outside the prefix: ${line}")
    endif()
    string(LENGTH "${GMAN_PREFIX}/" cut)
    string(SUBSTRING "${line}" ${cut} -1 rel)
    if(rel MATCHES "/$")
      string(REGEX REPLACE "/$" "" rel "${rel}")
      list(APPEND listed_dirs "${rel}")
    else()
      list(APPEND listed_files "${rel}")
    endif()
  endforeach()
  set(${files} "${listed_files}" PARENT_SCOPE)
  set(${dirs} "${listed_dirs}" PARENT_SCOPE)
endfunction()

# --- Step 1: stage a tarball tree as release.yml's "Stage the install tree" --

file(REMOVE_RECURSE "${GMAN_WORK_DIR}")
file(MAKE_DIRECTORY "${GMAN_RENDER_DIR}")
gman_expect_exit("step 1 (stage): cmake --install" 0
  COMMAND "${CMAKE_COMMAND}" --install "${GMAN_BUILD_DIR}"
          --prefix "${GMAN_STAGE}")
file(COPY "${GMAN_SOURCE_DIR}/packaging/install.sh"
  DESTINATION "${GMAN_STAGE}")
file(COPY "${GMAN_SOURCE_DIR}/samples/vase.rib"
  DESTINATION "${GMAN_STAGE}/samples")

# --- Step 2: install beside an unrelated file, render, read the manifest -----

set(GMAN_SEED "${GMAN_PREFIX}/bin/unrelated")
file(WRITE "${GMAN_SEED}" "not gman's\n")

gman_expect_exit("step 2 (install): install.sh" 0
  COMMAND "${GMAN_INSTALLER}" --prefix "${GMAN_PREFIX}")

if(EXISTS "${GMAN_PREFIX}/install.sh" OR EXISTS "${GMAN_PREFIX}/samples")
  message(FATAL_ERROR "step 2 (install): install.sh or samples/ was installed")
endif()

# The installed gman renders the sample from the stage folder. No
# LD_LIBRARY_PATH is set, for the reason tests/install_test.cmake gives.
set(GMAN_RENDER_ARGS "")
if(GMAN_HAS_RAYTRACER)
  set(GMAN_RENDER_ARGS -r gmanraytracer)
endif()
gman_expect_exit("step 2 (render): vase.rib" 0
  COMMAND "${GMAN_PREFIX}/bin/gman" ${GMAN_RENDER_ARGS}
          "${GMAN_STAGE}/samples/vase.rib"
  WORKING_DIRECTORY "${GMAN_RENDER_DIR}")
if(NOT EXISTS "${GMAN_RENDER_DIR}/vase.png")
  message(FATAL_ERROR "step 2 (render): vase.rib did not write vase.png")
endif()
file(SIZE "${GMAN_RENDER_DIR}/vase.png" size)
if(size EQUAL 0)
  message(FATAL_ERROR "step 2 (render): vase.png is empty")
endif()

# The manifest names every file and directory the install added, and no
# sample.
if(NOT EXISTS "${GMAN_MANIFEST}")
  message(FATAL_ERROR "step 2 (manifest): ${GMAN_MANIFEST} does not exist")
endif()
gman_list_prefix(after_install)
gman_read_manifest(listed_files listed_dirs)
set(expected ${listed_files} ${listed_dirs})
list(APPEND expected "share/gman/install_manifest.txt" "bin/unrelated" "bin")
list(REMOVE_DUPLICATES expected)
list(SORT expected)
if(NOT after_install STREQUAL expected)
  message(FATAL_ERROR
    "step 2 (manifest): the prefix holds\n${after_install}\nthe manifest lists\n${expected}")
endif()
if(NOT "bin/gman" IN_LIST listed_files)
  message(FATAL_ERROR "step 2 (manifest): bin/gman is not listed")
endif()
if("bin/unrelated" IN_LIST listed_files OR "bin" IN_LIST listed_dirs)
  message(FATAL_ERROR "step 2 (manifest): it lists what the install did not write")
endif()
foreach(entry IN LISTS listed_files)
  if(entry MATCHES "^samples/|vase\\.rib|install\\.sh")
    message(FATAL_ERROR "step 2 (manifest): it lists ${entry}")
  endif()
endforeach()
file(READ "${GMAN_MANIFEST}" first_manifest)

# --- Step 3: a repeat install replaces the first, stale file and all ---------

file(WRITE "${GMAN_PREFIX}/lib/libstale.so" "left by an older gman\n")
file(APPEND "${GMAN_MANIFEST}" "${GMAN_PREFIX}/lib/libstale.so\n")
gman_expect_exit("step 3 (reinstall): install.sh" 0
  COMMAND "${GMAN_INSTALLER}" --prefix "${GMAN_PREFIX}")
file(READ "${GMAN_MANIFEST}" second_manifest)
if(NOT second_manifest STREQUAL first_manifest)
  message(FATAL_ERROR
    "step 3 (reinstall): the manifest changed\n${first_manifest}\n--\n${second_manifest}")
endif()
if(EXISTS "${GMAN_PREFIX}/lib/libstale.so")
  message(FATAL_ERROR "step 3 (reinstall): the older install's file survived")
endif()
gman_list_prefix(after_reinstall)
if(NOT after_reinstall STREQUAL after_install)
  message(FATAL_ERROR "step 3 (reinstall): the prefix's contents changed")
endif()

# --- Step 4: uninstall leaves the prefix as it was seeded --------------------

gman_expect_exit("step 4 (uninstall): install.sh --uninstall" 0
  COMMAND "${GMAN_INSTALLER}" --prefix "${GMAN_PREFIX}" --uninstall)
gman_list_prefix(after_uninstall)
set(seeded "bin" "bin/unrelated")
if(NOT after_uninstall STREQUAL seeded)
  message(FATAL_ERROR
    "step 4 (uninstall): the prefix holds\n${after_uninstall}\nexpected\n${seeded}")
endif()

# --- Step 5: a refused invocation exits 2 and writes nothing -----------------

set(GMAN_UNUSED_PREFIX "${GMAN_WORK_DIR}/unused")
gman_expect_exit("step 5 (refuse): install.sh --bogus" 2
  COMMAND "${GMAN_INSTALLER}" --prefix "${GMAN_UNUSED_PREFIX}" --bogus)
gman_expect_exit("step 5 (refuse): install.sh --prefix with no value" 2
  COMMAND "${GMAN_INSTALLER}" --prefix)
file(MAKE_DIRECTORY "${GMAN_WORK_DIR}/bare")
file(COPY "${GMAN_SOURCE_DIR}/packaging/install.sh"
  DESTINATION "${GMAN_WORK_DIR}/bare")
gman_expect_exit("step 5 (refuse): install.sh with no bin/gman" 2
  COMMAND "${GMAN_WORK_DIR}/bare/install.sh" --prefix "${GMAN_UNUSED_PREFIX}")
if(EXISTS "${GMAN_UNUSED_PREFIX}")
  message(FATAL_ERROR "step 5 (refuse): a refused install wrote ${GMAN_UNUSED_PREFIX}")
endif()
