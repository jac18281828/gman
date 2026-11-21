# SPDX-License-Identifier: LGPL-2.1-or-later

# clang-tidy over every production translation unit, run in parallel; any
# finding fails the target. tests/ is exempt (AGENTS.md) and gmansl/ builds no
# compile_commands.json entry to run against. compile_commands.json paths are
# absolute, so filtering must strip the source root first and anchor at ^ -- a
# bare directory-name search also matches a checkout directory named after the
# repo (e.g. /home/runner/work/gman/gman/...), which would let every tests/
# entry through too.
set(GMAN_CLANG_TIDY_MAJOR "22")
set(GMAN_TIDY_HEADER_DIRS ${GMAN_SOURCE_DIRS})
list(REMOVE_ITEM GMAN_TIDY_HEADER_DIRS gmansl)
set(GMAN_TIDY_SOURCE_DIRS ${GMAN_TIDY_HEADER_DIRS})
list(REMOVE_ITEM GMAN_TIDY_SOURCE_DIRS include)
string(JOIN "|" GMAN_TIDY_SOURCE_DIR_REGEX ${GMAN_TIDY_SOURCE_DIRS})
string(JOIN "|" GMAN_TIDY_HEADER_DIR_REGEX ${GMAN_TIDY_HEADER_DIRS})

find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)

set(GMAN_CLANG_TIDY_FOUND_VERSION "none")
set(GMAN_CLANG_TIDY_FOUND_MAJOR "")
if(CLANG_TIDY_EXECUTABLE)
  execute_process(
    COMMAND "${CLANG_TIDY_EXECUTABLE}" --version
    OUTPUT_VARIABLE GMAN_CLANG_TIDY_FOUND_VERSION_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+"
    GMAN_CLANG_TIDY_FOUND_VERSION "${GMAN_CLANG_TIDY_FOUND_VERSION_RAW}")
  string(REGEX MATCH "^[0-9]+"
    GMAN_CLANG_TIDY_FOUND_MAJOR "${GMAN_CLANG_TIDY_FOUND_VERSION}")
endif()

set(GMAN_TIDY_SYSROOT "")
if(APPLE)
  if(CMAKE_OSX_SYSROOT)
    set(GMAN_TIDY_SYSROOT "${CMAKE_OSX_SYSROOT}")
  else()
    execute_process(
      COMMAND xcrun --show-sdk-path
      OUTPUT_VARIABLE GMAN_TIDY_SYSROOT
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
endif()

if(NOT GMAN_CLANG_TIDY_FOUND_MAJOR STREQUAL GMAN_CLANG_TIDY_MAJOR)
  add_custom_target(tidy-check
    COMMAND "${CMAKE_COMMAND}" -E echo
      "tidy-check requires clang-tidy ${GMAN_CLANG_TIDY_MAJOR}, found ${GMAN_CLANG_TIDY_FOUND_VERSION}; uv tool install clang-tidy==22.1.8"
    COMMAND "${CMAKE_COMMAND}" -E false
    COMMENT "Checking the clang-tidy version tidy-check requires"
    VERBATIM)
else()
  add_custom_target(tidy-check
    COMMAND sh "${CMAKE_CURRENT_LIST_DIR}/tidycheck.sh"
      "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CLANG_TIDY_EXECUTABLE}"
      "${CMAKE_COMMAND}" "${GMAN_TIDY_SYSROOT}"
      "${GMAN_TIDY_SOURCE_DIR_REGEX}" "${GMAN_TIDY_HEADER_DIR_REGEX}"
      ${GMAN_TIDY_HEADER_DIRS}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running clang-tidy over every production source and header"
    VERBATIM)
endif()
