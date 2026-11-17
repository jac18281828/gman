# SPDX-License-Identifier: LGPL-2.1-or-later
#
# The consumer half of the "install" test (see tests/install_test.cmake): a
# second, unrelated CMake project that finds gman purely through
# find_package(gman), the way any programmer linking an installed gman
# would. GMAN_CONSUMER_VERSION is the parent build's PROJECT_VERSION, passed
# in by the driver script.

cmake_minimum_required(VERSION 3.21)

project(gman_install_consumer LANGUAGES C CXX)

find_package(gman ${GMAN_CONSUMER_VERSION} CONFIG REQUIRED)

add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE gman::gman_core)

add_executable(consumer_ray consumer_ray.cpp)
target_link_libraries(consumer_ray PRIVATE gman::gman_ray)

add_executable(consumer_raster consumer_raster.cpp)
target_link_libraries(consumer_raster PRIVATE gman::gman_raster)

# Plugins are built and linked here, never loaded: the installed headers and
# exported symbols are all an out-of-tree shader or renderer needs.
add_library(shader_plugin MODULE shader_plugin.cpp)
target_link_libraries(shader_plugin PRIVATE gman::gman_core)
set_target_properties(shader_plugin PROPERTIES PREFIX "lib" SUFFIX ".so")

add_library(renderer_plugin MODULE renderer_plugin.cpp)
target_link_libraries(renderer_plugin PRIVATE gman::gman_core gman::gman_ray)
set_target_properties(renderer_plugin PROPERTIES PREFIX "lib" SUFFIX ".so")
