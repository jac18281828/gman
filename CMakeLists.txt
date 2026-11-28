# SPDX-License-Identifier: LGPL-2.1-or-later
#
# GMAN — a RenderMan-compatible renderer.

cmake_minimum_required(VERSION 3.24)

project(gman
  VERSION 1.5.0
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
include(CMakePackageConfigHelpers)

# Tests reach internals through the shared library, so a test build exports
# every symbol.
if(NOT BUILD_TESTING)
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)
endif()

# Every installed library and plugin carries the same run path, relative to
# itself: a release tarball is extracted wherever the user puts it, and a path
# naming the prefix it was configured with would send the loader somewhere
# that does not exist on their machine. A prefix install still resolves,
# because the relative path lands in the same libdir either way. A plugin
# needs it as much as the driver does: the loader resolves a dependency
# through the run path of the object that names it, and a ray renderer names
# libgman_ray, which the driver does not load. Set before the first target, so
# every target created below starts from it.
if(APPLE)
  set(GMAN_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
else()
  set(GMAN_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
set(CMAKE_INSTALL_RPATH "${GMAN_INSTALL_RPATH}")

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

include(FetchContent)
FetchContent_Declare(CLI11
  URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.7.2.tar.gz
  URL_HASH SHA256=46eef3101da70852ec7af026e09d485ccee81813331c8c6052d39344443b83da
  FIND_PACKAGE_ARGS 2.4 CONFIG)
FetchContent_MakeAvailable(CLI11)

# radiosity builds the ray tracer's indirect-light pass, loaded through Option
# "render" "string indirect" ["radiosity"]; it defaults on.
option(GMAN_BUILD_RAYTRACER "Build the ray tracer plugin" ON)
option(GMAN_BUILD_RADIOSITY "Build the radiosity indirect-light pass plugin" ON)
option(GMAN_BUILD_PATHTRACER "Build the path tracer plugin" ON)

# GMAN_LTO links every target with link-time optimization when the toolchain
# supports it and configures without it otherwise; the release preset sets it.
option(GMAN_LTO "Link with link-time optimization where the toolchain supports it" OFF)
if(GMAN_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT GMAN_IPO_SUPPORTED OUTPUT GMAN_IPO_OUTPUT
    LANGUAGES C CXX)
  if(GMAN_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    string(REPLACE "\n" " " GMAN_IPO_REASON "${GMAN_IPO_OUTPUT}")
    message(STATUS "GMAN_LTO is ON; building without LTO, IPO unsupported: ${GMAN_IPO_REASON}")
  endif()
endif()

# The production source directories, relative to the source root. format-check
# and every test that scans source read this one list, so a new directory is
# added here once. Every consumer scans recursively, so libgmanray/include and
# libgmanraster/include need no entry of their own.
set(GMAN_SOURCE_DIRS
  include libgman libgmanray libgmanraster libgmanrib shaders renderers gman gmansl)

# ---------------------------------------------------------------------------
# Optional file-Display drivers -- one gman_add_file_driver(<name>) call per
# driver. It declares GMAN_WITH_<NAME>, defaulting to whether <name>'s
# library was found; a FATAL_ERROR names that library when the option is
# forced ON without it; once enabled, it adds the driver's source and links
# its library PRIVATE on the gman library. OPT_IN instead defaults the option OFF
# and looks for the library, with the FIND_PACKAGE arguments, only when the
# option is ON, so a default configure skips that library's CMake
# files; LIBRARY names the library in the FATAL_ERROR and TARGET is the
# imported target to link. It also sets the
# GMAN_<NAME>_INCLUDE/FACTORY_DEFINITION/FACTORY_REF/MATCHER variables
# gmanfiledrivers.cpp.in's configure_file below substitutes -- empty and
# "nullptr" when disabled, so the generated table needs no preprocessor
# conditional for a choice CMake already made. MATCHER is the type test the table
# reads to name a driver from its object. Requires the gman target to already
# exist.
# ---------------------------------------------------------------------------

function(gman_add_file_driver name)
  cmake_parse_arguments(PARSE_ARGV 1 driver "OPT_IN" "LIBRARY;TARGET" "FIND_PACKAGE")
  string(TOUPPER "${name}" upper)
  set(libraryName "lib${name}")
  set(linkTarget "${upper}::${upper}")
  if(driver_LIBRARY)
    set(libraryName "${driver_LIBRARY}")
  endif()
  if(driver_TARGET)
    set(linkTarget "${driver_TARGET}")
  endif()

  if(driver_OPT_IN)
    option(GMAN_WITH_${upper} "Build the ${upper} display driver" OFF)
    if(GMAN_WITH_${upper})
      find_package(${driver_FIND_PACKAGE})
    endif()
    set(libraryFound FALSE)
    if(TARGET ${linkTarget})
      set(libraryFound TRUE)
    endif()
  else()
    option(GMAN_WITH_${upper} "Build the ${upper} display driver" ${${upper}_FOUND})
    set(libraryFound ${${upper}_FOUND})
  endif()
  if(GMAN_WITH_${upper} AND NOT libraryFound)
    message(FATAL_ERROR "GMAN_WITH_${upper} is ON but ${libraryName} was not found")
  endif()

  set(GMAN_${upper}_INCLUDE "" PARENT_SCOPE)
  set(GMAN_${upper}_FACTORY_DEFINITION "" PARENT_SCOPE)
  set(GMAN_${upper}_FACTORY_REF "nullptr" PARENT_SCOPE)
  set(GMAN_${upper}_MATCHER "nullptr" PARENT_SCOPE)
  if(NOT GMAN_WITH_${upper})
    return()
  endif()

  set(GMAN_${upper}_INCLUDE "#include \"gmanoutput${name}.h\"" PARENT_SCOPE)
  set(GMAN_${upper}_FACTORY_DEFINITION
"std::unique_ptr<GMANOutput> gmanMake${upper}Output(char const *path,
                                          int width, int height) {
  return std::make_unique<gman::Output${upper}>(path, width, height);
}" PARENT_SCOPE)
  set(GMAN_${upper}_FACTORY_REF "&gmanMake${upper}Output" PARENT_SCOPE)
  set(GMAN_${upper}_MATCHER "&isOutputOf<gman::Output${upper}>" PARENT_SCOPE)

  target_sources(gman PRIVATE "libgman/gmanoutput${name}.cpp")
  target_link_libraries(gman PRIVATE ${linkTarget})
endfunction()

# ---------------------------------------------------------------------------
# libgman — the renderer front end and its support library
# ---------------------------------------------------------------------------

add_library(gman SHARED
  libgman/gmanarealightwarning.cpp
  libgman/gmanattributes.cpp
  libgman/gmanbasis.cpp
  libgman/gmanbbox.cpp
  libgman/gmanbitmap.cpp
  libgman/gmanbsdf.cpp
  libgman/gmancapi.cpp
  libgman/gmancolorsamples.cpp
  libgman/gmancontext.cpp
  libgman/gmandefaults.cpp
  libgman/gmandictionary.cpp
  libgman/gmandisplacementshader.cpp
  libgman/gmanerror.cpp
  libgman/gmanfilters.cpp
  libgman/gmanframebuffer.cpp
  libgman/gmangetarguments.cpp
  libgman/gmangraphicstate.cpp
  libgman/gmanhpoint.cpp
  libgman/gmanimagerpass.cpp
  libgman/gmanimagershader.cpp
  libgman/gmaninlineparse.cpp
  libgman/gmanlightsourcemgr.cpp
  libgman/gmanlightsourceshader.cpp
  libgman/gmanlinearworldmanager.cpp
  libgman/gmanloadable.cpp
  libgman/gmanloadablerenderer.cpp
  libgman/gmanloadablerenderman.cpp
  libgman/gmanloadableshader.cpp
  libgman/gmanlog.cpp
  libgman/gmanmatrix4.cpp
  libgman/gmannoise.cpp
  libgman/gmannormal.cpp
  libgman/gmanoptions.cpp
  libgman/gmanoutput.cpp
  libgman/gmanoutputpnm.cpp
  libgman/gmanoutputtiff.cpp
  libgman/gmanoutputx11.cpp
  libgman/gmanparallel.cpp
  libgman/gmanparameterlist.cpp
  libgman/gmanpoint.cpp
  libgman/gmanpolygon.cpp
  libgman/gmanpolygonmesh.cpp
  libgman/gmanprimitive.cpp
  libgman/gmanprimitivefactory.cpp
  libgman/gmanprimitives.cpp
  libgman/gmanrenderer.cpp
  libgman/gmanrenderman.cpp
  libgman/gmanrendermanimpl.cpp
  libgman/gmanribparse.cpp
  libgman/gmanribtokenize.cpp
  libgman/gmansamplebuffer.cpp
  libgman/gmansampling.cpp
  libgman/gmanshader.cpp
  libgman/gmanshaderenvironment.cpp
  libgman/gmanshading.cpp
  libgman/gmanslapi.cpp
  libgman/gmansurfaceshader.cpp
  libgman/gmantexture.cpp
  libgman/gmantiff.cpp
  libgman/gmantoken.cpp
  libgman/gmantransform.cpp
  libgman/gmantrimcurve.cpp
  libgman/gmanvector.cpp
  libgman/gmanvector4.cpp
  libgman/gmanviewingsystem.cpp
  libgman/gmanviewingsysteminputs.cpp
  libgman/gmanvolumeshader.cpp
  libgman/gmanvsorthographic.cpp
  libgman/gmanvsperspective.cpp
  libgman/gmanwindowoutput.cpp
  libgman/gmanworld.cpp
  libgman/gmanworldmanager.cpp)

# libgman's export header; GMAN_EXPORT replaces the MSVC-era
# GMANDLL/NOTGMANDLL macro maze.
# Under include/ rather than directly in the binary dir: a bare "gman"
# directory there would collide with the gman executable.
generate_export_header(gman
  BASE_NAME GMAN
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/gman/export.h")

# The installed headers quote-include "gman/export.h" and each other, so an
# install-interface consumer needs only the include root, not the two
# source/binary directories the build tree carries; a bare "ri.h" on every
# consumer's path would collide with other RenderMan SDKs' own.
target_include_directories(gman PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# CMAKE_CXX_STANDARD is a directory variable that an exported target does not
# carry; installed headers such as gmanlog.h use <format>, so a consumer on
# a compiler defaulting to an older standard must inherit this floor.
target_compile_features(gman PUBLIC cxx_std_20)

# The generated driver table (build tree, not libgman/) quote-includes
# gmanfiledrivers.h and the driver headers as if it lived beside them.
target_include_directories(gman PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/libgman")

# HAVE_LIBDL is not an optional driver: it guards the whole body of
# GMANLoadable's constructor and loadSymbol. Undefined, the plugin loader
# compiles and silently loads nothing.
target_compile_definitions(gman PUBLIC HAVE_LIBDL)

target_link_libraries(gman PUBLIC ZLIB::ZLIB Threads::Threads ${CMAKE_DL_LIBS})
target_link_libraries(gman PRIVATE TIFF::TIFF)

gman_add_file_driver(png)
gman_add_file_driver(jpeg)
gman_add_file_driver(exr OPT_IN LIBRARY OpenEXR TARGET OpenEXR::OpenEXR FIND_PACKAGE OpenEXR 3.1 CONFIG)

# The file-Display driver table -- see libgman/gmanfiledrivers.cpp.in for
# the design; fixed order tiff, pnm, png, jpeg, exr.
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/libgman/gmanfiledrivers.cpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/gmanfiledrivers.cpp"
  @ONLY)
target_sources(gman PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated/gmanfiledrivers.cpp")

# ---------------------------------------------------------------------------
# gman_ray — what every ray renderer shares: intersection, traversal,
# occlusion, emitters and the indirect-light pass seam
# ---------------------------------------------------------------------------

add_library(gman_ray SHARED
  libgmanray/gmanemitter.cpp
  libgmanray/gmanindirectpass.cpp
  libgmanray/gmanraybvh.cpp
  libgmanray/gmanraycone.cpp
  libgmanray/gmanraycylinder.cpp
  libgmanray/gmanraydisk.cpp
  libgmanray/gmanrayhyperboloid.cpp
  libgmanray/gmanrayoccluder.cpp
  libgmanray/gmanrayparaboloid.cpp
  libgmanray/gmanraypolygon.cpp
  libgmanray/gmanraypolygonmesh.cpp
  libgmanray/gmanrayprimitive.cpp
  libgmanray/gmanrayprimitivefactory.cpp
  libgmanray/gmanrayquartic.cpp
  libgmanray/gmanraysphere.cpp
  libgmanray/gmanraytorus.cpp
  libgmanray/gmanrenderprogress.cpp)

# GMANRAY_EXPORT tags what the ray headers declare; the plugin entry points
# keep GMAN_EXPORT. Beside gman/export.h, so libgman's public include
# directories reach it.
generate_export_header(gman_ray
  BASE_NAME GMANRAY
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/gman/rayexport.h")

# Ray's public headers sit in libgmanray/include; gmanrayquadratic.h and
# gmanrayquartic.h are private headers beside the sources. The installed
# headers land flat beside core's, so the install interface needs no entry of
# its own: libgman's install include root, which this link carries, serves
# them.
target_link_libraries(gman_ray PUBLIC gman)
target_include_directories(gman_ray PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libgmanray/include>")

# ---------------------------------------------------------------------------
# gman_raster — what the rasterizing renderers share: the body, face and
# vertex model, the patch and polygon object manager that tessellates into
# it, and the polygon clipper
# ---------------------------------------------------------------------------

add_library(gman_raster SHARED
  libgmanraster/gmanbody.cpp
  libgmanraster/gmanclipedge.cpp
  libgmanraster/gmanface.cpp
  libgmanraster/gmanobject.cpp
  libgmanraster/gmanoutputpolygon.cpp
  libgmanraster/gmanpolygonclipper.cpp
  libgmanraster/gmanrasterprimitivefactory.cpp)

# GMANRASTER_EXPORT tags what the raster headers declare; the plugin entry
# points keep GMAN_EXPORT. Beside gman/export.h, so libgman's public include
# directories reach it.
generate_export_header(gman_raster
  BASE_NAME GMANRASTER
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/gman/rasterexport.h")

# Raster's public headers sit in libgmanraster/include, and every raster source
# includes public headers alone. The installed headers land flat beside core's,
# so the install interface needs no entry of its own: libgman's install
# include root, which this link carries, serves them.
target_link_libraries(gman_raster PUBLIC gman)
target_include_directories(gman_raster PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libgmanraster/include>")

# ---------------------------------------------------------------------------
# Renderer plugins
# ---------------------------------------------------------------------------

function(gman_add_plugin target)
  add_library(${target} MODULE ${ARGN})
  target_link_libraries(${target} PRIVATE gman)
  set_target_properties(${target} PROPERTIES
    PREFIX "lib"
    SUFFIX ".so")
endfunction()

gman_add_plugin(gman_zbuffer
  renderers/zbuffer/gmanzbufferrenderer.cpp
  renderers/zbuffer/zbufferloader.cpp)
target_link_libraries(gman_zbuffer PRIVATE gman_raster)
set_target_properties(gman_zbuffer PROPERTIES OUTPUT_NAME "gmanzbuffer")

# The ray tracer's own render loop -- GMANRayTracer and GMANRaytraceRenderer
# -- shared by the gman_raytracer plugin below and by the unit tests that
# exercise them in tests/CMakeLists.txt. Ray geometry, the BVH and the
# transmission walk compile into gman_ray instead, unconditionally; this
# object library holds only what stays specific to the ray tracer plugin.
# Built whenever GMAN_BUILD_RAYTRACER or BUILD_TESTING is ON, outside
# the GMAN_BUILD_RAYTRACER guard, because those unit tests must still build
# and run with the raytracer off.
if(GMAN_BUILD_RAYTRACER OR BUILD_TESTING)
  add_library(gman_raytracer_objects OBJECT
    renderers/raytracer/gmanraytracerenderer.cpp)
  target_link_libraries(gman_raytracer_objects PUBLIC gman gman_ray)
  target_include_directories(gman_raytracer_objects PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/raytracer")
  # OBJECT libraries do not get PIC automatically the way SHARED and MODULE
  # targets do; without it, linking these objects into gman_raytracer (a
  # MODULE) fails or silently succeeds only on some platforms.
  set_target_properties(gman_raytracer_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
endif()

if(GMAN_BUILD_RAYTRACER)
  gman_add_plugin(gman_raytracer renderers/raytracer/rayloader.cpp)
  target_link_libraries(gman_raytracer PRIVATE gman_raytracer_objects)
  set_target_properties(gman_raytracer PROPERTIES OUTPUT_NAME "gmanraytracer")
endif()

# GMANPathtraceRenderer, gman_raytracer_objects' pattern above: built
# whenever GMAN_BUILD_PATHTRACER or BUILD_TESTING is ON, outside the
# GMAN_BUILD_PATHTRACER guard, because those unit tests must still build
# and run with the plugin off.
if(GMAN_BUILD_PATHTRACER OR BUILD_TESTING)
  add_library(gman_pathtracer_objects OBJECT
    renderers/pathtracer/gmandenoise.cpp
    renderers/pathtracer/gmanpathtracerenderer.cpp)
  target_link_libraries(gman_pathtracer_objects PUBLIC gman gman_ray)
  target_include_directories(gman_pathtracer_objects PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/pathtracer")
  set_target_properties(gman_pathtracer_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
endif()

if(GMAN_BUILD_PATHTRACER)
  gman_add_plugin(gman_pathtracer renderers/pathtracer/pathloader.cpp)
  target_link_libraries(gman_pathtracer PRIVATE gman_pathtracer_objects)
  set_target_properties(gman_pathtracer PROPERTIES OUTPUT_NAME "gmanpathtracer")
endif()

# The radiosity solver, its element mesh and the ray tracer's indirect-
# light pass over them, gman_raytracer_objects' pattern above: built
# whenever GMAN_BUILD_RADIOSITY or BUILD_TESTING is on, so their unit
# tests still build and run with the plugin off. Every tests-on build
# then compiles the pass, CI's included.
if(GMAN_BUILD_RADIOSITY OR BUILD_TESTING)
  add_library(gman_radiosity_objects OBJECT
    renderers/radiosity/gmanradiositymesh.cpp
    renderers/radiosity/gmanradiositypass.cpp
    renderers/radiosity/gmanradiositysolver.cpp)
  target_link_libraries(gman_radiosity_objects PUBLIC gman gman_ray)
  target_include_directories(gman_radiosity_objects PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/renderers/radiosity")
  set_target_properties(gman_radiosity_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
endif()

# The token "radiosity" (Option "render" "string indirect" ["radiosity"])
# opens "libradiosity.so" by gman::loadIndirectPass's bare-name rule,
# hence OUTPUT_NAME "radiosity" rather than this target's name.
if(GMAN_BUILD_RADIOSITY)
  gman_add_plugin(gman_radiosity
    renderers/radiosity/radiosityloader.cpp)
  target_link_libraries(gman_radiosity PRIVATE gman_radiosity_objects)
  set_target_properties(gman_radiosity PROPERTIES OUTPUT_NAME "radiosity")
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

gman_add_plugin(gman_mirror shaders/gmanmirror.cpp)
set_target_properties(gman_mirror PROPERTIES OUTPUT_NAME "mirror")

gman_add_plugin(gman_glass shaders/gmanglass.cpp)
set_target_properties(gman_glass PROPERTIES OUTPUT_NAME "glass")

gman_add_plugin(gman_background shaders/gmanbackground.cpp)
set_target_properties(gman_background PROPERTIES OUTPUT_NAME "background")

# ---------------------------------------------------------------------------
# gman_rib — the RIB writer, loaded the way a renderer is
# ---------------------------------------------------------------------------

gman_add_plugin(gman_rib
  libgmanrib/gmanascii.cpp
  libgmanrib/ribloader.cpp)
set_target_properties(gman_rib PROPERTIES OUTPUT_NAME "gmanrib")

# ---------------------------------------------------------------------------
# gman — the driver
# ---------------------------------------------------------------------------

add_library(gman_commandline_module STATIC gman/gmancommandline.cpp)
target_include_directories(gman_commandline_module PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/gman")
target_link_libraries(gman_commandline_module PUBLIC gman PRIVATE CLI11::CLI11)

add_executable(gman_cli gman/gman.cpp)
set_target_properties(gman_cli PROPERTIES OUTPUT_NAME gman)
target_link_libraries(gman_cli PRIVATE gman gman_commandline_module)

# gman --version reports gman::fileDrivers(), the driver table's own private
# contract, and PROJECT_VERSION -- neither is part of libgman's public API.
target_include_directories(gman_cli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libgman")
target_compile_definitions(gman_cli PRIVATE
  GMAN_PROJECT_VERSION="${PROJECT_VERSION}")

# RiBegin dlopen's "lib<renderer>.so" by bare name, so the build tree itself
# has to be on the runtime search path or no renderer ever loads.
set_target_properties(gman_cli gman PROPERTIES
  BUILD_RPATH "${CMAKE_BINARY_DIR}")

add_dependencies(gman_cli gman_zbuffer gman_matte gman_plastic gman_metal gman_paintedplastic gman_shinymetal
                 gman_mirror gman_glass gman_background gman_rib)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

# gman, gman_ray and gman_raster join the export set: they are what a
# programmer links. gman_rib is a plugin, dlopen'd like gman_zbuffer, and
# installs beside the other plugins below.
install(TARGETS gman gman_ray gman_raster
  EXPORT gmanTargets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(TARGETS gman_cli gman_zbuffer gman_matte gman_plastic gman_metal gman_paintedplastic gman_shinymetal
                gman_mirror gman_glass gman_background gman_rib
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
if(GMAN_BUILD_RAYTRACER)
  install(TARGETS gman_raytracer
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
if(GMAN_BUILD_RADIOSITY)
  install(TARGETS gman_radiosity
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
if(GMAN_BUILD_PATHTRACER)
  install(TARGETS gman_pathtracer
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gman"
  FILES_MATCHING PATTERN "*.h")
install(DIRECTORY libgmanray/include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gman"
  FILES_MATCHING PATTERN "*.h")
install(DIRECTORY libgmanraster/include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gman"
  FILES_MATCHING PATTERN "*.h")
# generate_export_header writes export.h, rayexport.h and rasterexport.h into
# the build tree, so they install apart from the source-tree headers above.
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/gman/export.h"
              "${CMAKE_CURRENT_BINARY_DIR}/include/gman/rayexport.h"
              "${CMAKE_CURRENT_BINARY_DIR}/include/gman/rasterexport.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gman")

install(EXPORT gmanTargets
  FILE gmanTargets.cmake
  NAMESPACE gman::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/gman")

# Before 1.0 a minor release breaks ABI.
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/gmanConfigVersion.cmake"
  COMPATIBILITY SameMinorVersion)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gmanConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/gmanConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/gman")

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/gmanConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/gmanConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/gman")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Format check
# ---------------------------------------------------------------------------

# Every .c, .cpp and .h under GMAN_SOURCE_DIRS and tests, gathered with
# CONFIGURE_DEPENDS so a new file is checked without anyone widening a list.
# gmanfiledrivers.cpp.in is a template, not a C++ source: its .cpp.in
# extension already leaves it out. format-check only reads the tree.
set(GMAN_CLANG_FORMAT_MAJOR "23")
set(GMAN_FORMAT_CHECK_GLOBS "")
foreach(dir ${GMAN_SOURCE_DIRS} tests)
  list(APPEND GMAN_FORMAT_CHECK_GLOBS
    "${CMAKE_SOURCE_DIR}/${dir}/*.c"
    "${CMAKE_SOURCE_DIR}/${dir}/*.cpp"
    "${CMAKE_SOURCE_DIR}/${dir}/*.h")
endforeach()
file(GLOB_RECURSE GMAN_FORMAT_CHECK_SOURCES CONFIGURE_DEPENDS
  ${GMAN_FORMAT_CHECK_GLOBS})

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
  # A contributor's clang-format of another major version formats differently
  # from CI and the dev container, so the gate would disagree with CI while
  # reading green. Patch releases within a major format alike.
  execute_process(
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" --version
    OUTPUT_VARIABLE GMAN_CLANG_FORMAT_FOUND_VERSION_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+"
    GMAN_CLANG_FORMAT_FOUND_VERSION "${GMAN_CLANG_FORMAT_FOUND_VERSION_RAW}")
  string(REGEX MATCH "^[0-9]+"
    GMAN_CLANG_FORMAT_FOUND_MAJOR "${GMAN_CLANG_FORMAT_FOUND_VERSION}")

  if(NOT GMAN_CLANG_FORMAT_FOUND_MAJOR STREQUAL GMAN_CLANG_FORMAT_MAJOR)
    add_custom_target(format-check
      COMMAND "${CMAKE_COMMAND}" -E echo
        "format-check requires clang-format ${GMAN_CLANG_FORMAT_MAJOR}, found ${GMAN_CLANG_FORMAT_FOUND_VERSION}; pip install clang-format==23.1.5"
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

include("${CMAKE_SOURCE_DIR}/cmake/tidycheck.cmake")
