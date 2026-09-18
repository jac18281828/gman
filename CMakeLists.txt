# SPDX-License-Identifier: LGPL-2.1-or-later
#
# GMAN — a RenderMan-compatible renderer.

cmake_minimum_required(VERSION 3.21)

project(gman
  VERSION 0.8.2
  DESCRIPTION "GMAN — a RenderMan-compatible renderer"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
  message(FATAL_ERROR "GMAN is POSIX only. Windows support was removed in phase 0.")
endif()

include(GenerateExportHeader)
include(GNUInstallDirs)
include(CTest)
include(CheckCXXSourceCompiles)

# A compiler that accepts -std=c++20 with an older standard library fails
# deep in a template error instead of at configure time. Check both features
# the baseline needs so that failure surfaces as one clear message.
check_cxx_source_compiles("
#include <format>
#include <thread>
int main() {
  std::jthread t([] {});
  return std::format(\"{}\", 1).size() != 1;
}
" GMAN_HAS_CXX20_FORMAT_JTHREAD)
if(NOT GMAN_HAS_CXX20_FORMAT_JTHREAD)
  message(FATAL_ERROR "C++20 with <format>: GCC 13 or Clang 17 or newer")
endif()

# Plugins are dlopen'd by bare "lib<name>.so", so the loader must find them
# beside the executable in the build tree.
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(ZLIB REQUIRED)
find_package(PNG)
find_package(TIFF REQUIRED)
find_package(JPEG)
find_package(Threads REQUIRED)

# All three are non-functional and kept for the design intent a later phase may
# want. reyes additionally does not link -- it has a declared virtual with no
# definition -- so its option compiles but does not complete.
option(GMAN_BUILD_REYES "Build the (non-functional) REYES renderer plugin" OFF)
option(GMAN_BUILD_RAYTRACER "Build the ray tracer plugin, which intersects but does not yet render" OFF)
option(GMAN_BUILD_RADIOSITY "Build the (non-functional) radiosity plugin" OFF)

# ---------------------------------------------------------------------------
# Optional file-Display drivers -- one gman_add_file_driver(<name>) call per
# driver. It declares GMAN_WITH_<NAME>, defaulting to whether <name>'s
# library was found; a FATAL_ERROR names that library when the option is
# forced ON without it; once enabled, it adds the driver's source and links
# its library PRIVATE on gman_core. It also sets the
# GMAN_<NAME>_INCLUDE/FACTORY_DEFINITION/FACTORY_REF variables
# gmanfiledrivers.cpp.in's configure_file below substitutes -- empty and
# "nullptr" when disabled, so the generated table never has to preprocess a
# choice CMake already made. Requires gman_core to already exist.
# ---------------------------------------------------------------------------

function(gman_add_file_driver name)
  string(TOUPPER "${name}" upper)

  option(GMAN_WITH_${upper} "Build the ${upper} display driver" ${${upper}_FOUND})
  if(GMAN_WITH_${upper} AND NOT ${upper}_FOUND)
    message(FATAL_ERROR "GMAN_WITH_${upper} is ON but lib${name} was not found")
  endif()

  set(GMAN_${upper}_INCLUDE "" PARENT_SCOPE)
  set(GMAN_${upper}_FACTORY_DEFINITION "" PARENT_SCOPE)
  set(GMAN_${upper}_FACTORY_REF "nullptr" PARENT_SCOPE)
  if(NOT GMAN_WITH_${upper})
    return()
  endif()

  set(GMAN_${upper}_INCLUDE "#include \"gmanoutput${name}.h\"" PARENT_SCOPE)
  set(GMAN_${upper}_FACTORY_DEFINITION
"std::unique_ptr<GMANOutput> gmanMake${upper}Output(char const *path,
                                          int width, int height) {
  return std::make_unique<GMANOutput${upper}>(path, width, height);
}" PARENT_SCOPE)
  set(GMAN_${upper}_FACTORY_REF "&gmanMake${upper}Output" PARENT_SCOPE)

  target_sources(gman_core PRIVATE "libgman/gmanoutput${name}.cpp")
  target_link_libraries(gman_core PRIVATE ${upper}::${upper})
endfunction()

# ---------------------------------------------------------------------------
# gman_core — the renderer front end and its support library
# ---------------------------------------------------------------------------

add_library(gman_core SHARED
  libgman/gmanattributes.cpp
  libgman/gmanbasis.cpp
  libgman/gmanbbox.cpp
  libgman/gmanbitmap.cpp
  libgman/gmanbody.cpp
  libgman/gmancapi.cpp
  libgman/gmanclipedge.cpp
  libgman/gmancolor.cpp
  libgman/gmancolorsamples.cpp
  libgman/gmancontext.cpp
  libgman/gmandefaults.cpp
  libgman/gmandictionary.cpp
  libgman/gmandisplacementshader.cpp
  libgman/gmanerror.cpp
  libgman/gmanface.cpp
  libgman/gmanfilters.cpp
  libgman/gmanframebuffer.cpp
  libgman/gmangamma.cpp
  libgman/gmangetarguments.cpp
  libgman/gmangraphicstate.cpp
  libgman/gmanhpoint.cpp
  libgman/gmanimagershader.cpp
  libgman/gmaninlineparse.cpp
  libgman/gmanisoa.cpp
  libgman/gmanlightsourcemgr.cpp
  libgman/gmanlightsourceshader.cpp
  libgman/gmanlinearworldmanager.cpp
  libgman/gmanloadable.cpp
  libgman/gmanloadablerenderer.cpp
  libgman/gmanloadableshader.cpp
  libgman/gmanlog.cpp
  libgman/gmanmatrix4.cpp
  libgman/gmannoise.cpp
  libgman/gmannormal.cpp
  libgman/gmanobject.cpp
  libgman/gmanobjectmanager.cpp
  libgman/gmanoptions.cpp
  libgman/gmanoutput.cpp
  libgman/gmanoutputpnm.cpp
  libgman/gmanoutputpolygon.cpp
  libgman/gmanoutputtiff.cpp
  libgman/gmanoutputx11.cpp
  libgman/gmanparallel.cpp
  libgman/gmanparameterlist.cpp
  libgman/gmanpatchpolyobjectmanager.cpp
  libgman/gmanpoint.cpp
  libgman/gmanpolygon.cpp
  libgman/gmanpolygonclipper.cpp
  libgman/gmanprimitive.cpp
  libgman/gmanprimitives.cpp
  libgman/gmanquantize.cpp
  libgman/gmanrenderer.cpp
  libgman/gmanrenderman.cpp
  libgman/gmanrendermanimpl.cpp
  libgman/gmanribparse.cpp
  libgman/gmanribtokenize.cpp
  libgman/gmansamplebuffer.cpp
  libgman/gmansegment.cpp
  libgman/gmanshader.cpp
  libgman/gmanshaderenvironment.cpp
  libgman/gmanslapi.cpp
  libgman/gmansolidobject.cpp
  libgman/gmansurfaceshader.cpp
  libgman/gmantexture.cpp
  libgman/gmantiff.cpp
  libgman/gmantoken.cpp
  libgman/gmantools.cpp
  libgman/gmantransform.cpp
  libgman/gmantrimcurve.cpp
  libgman/gmanvector.cpp
  libgman/gmanvector4.cpp
  libgman/gmanvertex.cpp
  libgman/gmanvertex4.cpp
  libgman/gmanviewingsystem.cpp
  libgman/gmanvolumeshader.cpp
  libgman/gmanvsorthographic.cpp
  libgman/gmanvsperspective.cpp
  libgman/gmanwindowoutput.cpp
  libgman/gmanworldmanager.cpp)

# One export header for the whole project; GMAN_EXPORT replaces the MSVC-era
# GMANDLL/NOTGMANDLL macro maze.
# Under include/ rather than directly in the binary dir: a bare "gman"
# directory there would collide with the gman executable.
generate_export_header(gman_core
  BASE_NAME GMAN
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/gman/export.h")

target_include_directories(gman_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/include")

# The generated driver table (build tree, not libgman/) quote-includes
# gmanfiledrivers.h and the driver headers as if it lived beside them.
target_include_directories(gman_core PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/libgman")

# HAVE_LIBDL is not an optional driver: it guards the whole body of
# GMANLoadable's constructor and loadSymbol. Undefined, the plugin loader
# compiles and silently loads nothing.
target_compile_definitions(gman_core PUBLIC HAVE_LIBDL)

target_link_libraries(gman_core PUBLIC ZLIB::ZLIB Threads::Threads ${CMAKE_DL_LIBS})
target_link_libraries(gman_core PRIVATE TIFF::TIFF)

gman_add_file_driver(png)
gman_add_file_driver(jpeg)

# The file-Display driver table -- see libgman/gmanfiledrivers.cpp.in for
# the design; fixed order tiff, pnm, png, jpeg.
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/libgman/gmanfiledrivers.cpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/gmanfiledrivers.cpp"
  @ONLY)
target_sources(gman_core PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated/gmanfiledrivers.cpp")

# ---------------------------------------------------------------------------
# gman_rib — the RIB writer backend over the abstract RI interface
# ---------------------------------------------------------------------------

add_library(gman_rib SHARED
  libgmanrib/gmanascii.cpp)

target_link_libraries(gman_rib PUBLIC gman_core)

# ---------------------------------------------------------------------------
# Renderer plugins
# ---------------------------------------------------------------------------

function(gman_add_plugin target)
  add_library(${target} MODULE ${ARGN})
  target_link_libraries(${target} PRIVATE gman_core)
  set_target_properties(${target} PROPERTIES
    PREFIX "lib"
    SUFFIX ".so")
endfunction()

gman_add_plugin(gman_zbuffer
  renderers/zbuffer/gmanzbufferrenderer.cpp
  renderers/zbuffer/zbufferloader.cpp)
set_target_properties(gman_zbuffer PROPERTIES OUTPUT_NAME "gmanzbuffer")

if(GMAN_BUILD_REYES)
  gman_add_plugin(gman_reyes
    renderers/reyes/gmanreyesrenderer.cpp
    renderers/reyes/reyesloader.cpp)
  set_target_properties(gman_reyes PROPERTIES OUTPUT_NAME "gmanreyes")
endif()

if(GMAN_BUILD_RAYTRACER)
  gman_add_plugin(gman_raytracer
    renderers/raytracer/gmanrayinterface.cpp
    renderers/raytracer/gmanrayobjectmanager.cpp
    renderers/raytracer/gmanraysphere.cpp
    renderers/raytracer/gmanraytracerenderer.cpp
    renderers/raytracer/rayloader.cpp)
  set_target_properties(gman_raytracer PROPERTIES OUTPUT_NAME "gmanraytracer")
endif()

if(GMAN_BUILD_RADIOSITY)
  gman_add_plugin(gman_radiosity
    renderers/radiosity/gmanradiosityrenderer.cpp
    renderers/radiosity/radiosityloader.cpp)
  set_target_properties(gman_radiosity PROPERTIES OUTPUT_NAME "gmanradiosity")
endif()

gman_add_plugin(gman_matte shaders/gmanmatte.cpp)
set_target_properties(gman_matte PROPERTIES OUTPUT_NAME "matte")

gman_add_plugin(gman_plastic shaders/gmanplastic.cpp)
set_target_properties(gman_plastic PROPERTIES OUTPUT_NAME "plastic")

gman_add_plugin(gman_metal shaders/gmanmetal.cpp)
set_target_properties(gman_metal PROPERTIES OUTPUT_NAME "metal")

gman_add_plugin(gman_paintedplastic shaders/gmanpaintedplastic.cpp)
set_target_properties(gman_paintedplastic PROPERTIES OUTPUT_NAME "paintedplastic")

gman_add_plugin(gman_shinymetal shaders/gmanshinymetal.cpp)
set_target_properties(gman_shinymetal PROPERTIES OUTPUT_NAME "shinymetal")

# ---------------------------------------------------------------------------
# gman — the driver
# ---------------------------------------------------------------------------

add_executable(gman gman/gman.cpp)
target_link_libraries(gman PRIVATE gman_core)

# gman --version reports gmanFileDrivers(), the driver table's own private
# contract, and PROJECT_VERSION -- neither is part of gman_core's public API.
target_include_directories(gman PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libgman")
target_compile_definitions(gman PRIVATE
  GMAN_PROJECT_VERSION="${PROJECT_VERSION}")

# RiBegin dlopen's "lib<renderer>.so" by bare name, so the build tree itself
# has to be on the runtime search path or no renderer ever loads.
#
# The install rpath is relative to the executable, not absolute: a release
# tarball is extracted wherever the user puts it, and an rpath naming the
# prefix it was configured with would send the loader somewhere that does not
# exist on their machine. A prefix install still resolves, because the
# relative path lands in the same libdir either way.
if(APPLE)
  set(GMAN_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
else()
  set(GMAN_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
set_target_properties(gman gman_core gman_rib PROPERTIES
  BUILD_RPATH "${CMAKE_BINARY_DIR}"
  INSTALL_RPATH "${GMAN_INSTALL_RPATH}")

add_dependencies(gman gman_zbuffer gman_matte gman_plastic gman_metal gman_paintedplastic)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS gman gman_core gman_rib gman_zbuffer gman_matte gman_plastic gman_metal gman_paintedplastic
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gman"
  FILES_MATCHING PATTERN "*.h")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Format check
# ---------------------------------------------------------------------------

# Every .c, .cpp and .h under the eight source directories, gathered with
# CONFIGURE_DEPENDS so a new file is checked without anyone widening a list.
# gmanfiledrivers.cpp.in is a template, not a C++ source: its .cpp.in
# extension already leaves it out. Check only -- never reformats the tree.
set(GMAN_CLANG_FORMAT_VERSION "22.1.8")
set(GMAN_FORMAT_CHECK_DIRS
  include libgman libgmanrib shaders renderers gman gmansl tests)
set(GMAN_FORMAT_CHECK_GLOBS "")
foreach(dir ${GMAN_FORMAT_CHECK_DIRS})
  list(APPEND GMAN_FORMAT_CHECK_GLOBS
    "${CMAKE_SOURCE_DIR}/${dir}/*.c"
    "${CMAKE_SOURCE_DIR}/${dir}/*.cpp"
    "${CMAKE_SOURCE_DIR}/${dir}/*.h")
endforeach()
file(GLOB_RECURSE GMAN_FORMAT_CHECK_SOURCES CONFIGURE_DEPENDS
  ${GMAN_FORMAT_CHECK_GLOBS})

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
  # The pin in CI and the dev container would otherwise leave a
  # contributor's local run on another version, disagreeing with CI while
  # the gate reads green.
  execute_process(
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" --version
    OUTPUT_VARIABLE GMAN_CLANG_FORMAT_FOUND_VERSION_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+"
    GMAN_CLANG_FORMAT_FOUND_VERSION "${GMAN_CLANG_FORMAT_FOUND_VERSION_RAW}")

  if(NOT GMAN_CLANG_FORMAT_FOUND_VERSION STREQUAL GMAN_CLANG_FORMAT_VERSION)
    add_custom_target(format-check
      COMMAND "${CMAKE_COMMAND}" -E echo
        "format-check requires clang-format ${GMAN_CLANG_FORMAT_VERSION}, found ${GMAN_CLANG_FORMAT_FOUND_VERSION}"
      COMMAND "${CMAKE_COMMAND}" -E false
      COMMENT "Checking the clang-format version format-check requires"
      VERBATIM)
  else()
    add_custom_target(format-check
      COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror
        ${GMAN_FORMAT_CHECK_SOURCES}
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      COMMENT "Checking clang-format compliance of every source file in the tree"
      VERBATIM)
  endif()
endif()
