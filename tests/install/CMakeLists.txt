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
