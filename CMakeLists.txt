# SPDX-License-Identifier: LGPL-2.1-or-later
#
# GMAN — a RenderMan-compatible renderer.

cmake_minimum_required(VERSION 3.25)

project(gman
  VERSION 0.5.0
  DESCRIPTION "GMAN — a RenderMan-compatible renderer"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
  message(FATAL_ERROR "GMAN is POSIX only. Windows support was removed in phase 0.")
endif()

include(GenerateExportHeader)
include(GNUInstallDirs)
include(CTest)

# Plugins are dlopen'd by bare "lib<name>.so", so the loader must find them
# beside the executable in the build tree.
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(TIFF REQUIRED)
find_package(JPEG)
find_package(Threads REQUIRED)

option(GMAN_WITH_TIFF "Build the TIFF display driver" ON)
option(GMAN_WITH_PNG "Build the PNG display driver" ON)
option(GMAN_WITH_JPEG "Build the JPEG display driver" ${JPEG_FOUND})

option(GMAN_BUILD_REYES "Build the (non-functional) REYES renderer plugin" OFF)
option(GMAN_BUILD_RAYTRACER "Build the (non-functional) ray tracer plugin" OFF)
option(GMAN_BUILD_RADIOSITY "Build the (non-functional) radiosity plugin" OFF)

if(GMAN_WITH_JPEG AND NOT JPEG_FOUND)
  message(FATAL_ERROR "GMAN_WITH_JPEG is ON but libjpeg was not found")
endif()

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
  libgman/gmanguard.cpp
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
  libgman/gmanmutex.cpp
  libgman/gmannoise.cpp
  libgman/gmannormal.cpp
  libgman/gmanobject.cpp
  libgman/gmanobjectmanager.cpp
  libgman/gmanoptions.cpp
  libgman/gmanoutput.cpp
  libgman/gmanoutputjpeg.cpp
  libgman/gmanoutputpng.cpp
  libgman/gmanoutputpnm.cpp
  libgman/gmanoutputpolygon.cpp
  libgman/gmanoutputtiff.cpp
  libgman/gmanoutputx11.cpp
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
  libgman/gmanribparse.cpp
  libgman/gmanribtokenize.cpp
  libgman/gmansegment.cpp
  libgman/gmanshader.cpp
  libgman/gmanslapi.cpp
  libgman/gmansolidobject.cpp
  libgman/gmansurfaceshader.cpp
  libgman/gmanthread.cpp
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
  libgman/gmanworldmanager.cpp
  libgman/universalsuperclass.cpp)

# One export header for the whole project; GMAN_EXPORT replaces the MSVC-era
# GMANDLL/NOTGMANDLL macro maze.
generate_export_header(gman_core
  BASE_NAME GMAN
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/gman/export.h")

target_include_directories(gman_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}")

# HAVE_LIBDL is not an optional driver: it guards the whole body of
# GMANLoadable's constructor and loadSymbol. Undefined, the plugin loader
# compiles and silently loads nothing.
target_compile_definitions(gman_core PUBLIC HAVE_LIBDL)

target_link_libraries(gman_core PUBLIC ZLIB::ZLIB Threads::Threads ${CMAKE_DL_LIBS})

if(GMAN_WITH_TIFF)
  target_compile_definitions(gman_core PRIVATE HAVE_LIBTIFF)
  target_link_libraries(gman_core PRIVATE TIFF::TIFF)
endif()
if(GMAN_WITH_PNG)
  target_compile_definitions(gman_core PRIVATE HAVE_LIBPNG)
  target_link_libraries(gman_core PRIVATE PNG::PNG)
endif()
if(GMAN_WITH_JPEG)
  target_compile_definitions(gman_core PRIVATE HAVE_LIBJPEG)
  target_link_libraries(gman_core PRIVATE JPEG::JPEG)
endif()

# ---------------------------------------------------------------------------
# gman_rib — the RIB writer backend over the abstract RI interface
# ---------------------------------------------------------------------------

add_library(gman_rib SHARED
  libgmanrib/gmanascii.cpp
  libgmanrib/gmanrenderman.cpp)

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

# ---------------------------------------------------------------------------
# gman — the driver
# ---------------------------------------------------------------------------

add_executable(gman gman/gman.cpp)
target_link_libraries(gman PRIVATE gman_core)

# RiBegin dlopen's "lib<renderer>.so" by bare name, so the build tree itself
# has to be on the runtime search path or no renderer ever loads.
set_target_properties(gman gman_core gman_rib PROPERTIES
  BUILD_RPATH "${CMAKE_BINARY_DIR}"
  INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")

add_dependencies(gman gman_zbuffer gman_matte)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS gman gman_core gman_rib gman_zbuffer gman_matte
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
