# SPDX-License-Identifier: LGPL-2.1-or-later

# Idempotent safety net for tests/ribwriterfailure.c: if PLUGIN's
# renamed-aside copy is still there -- the test crashed or timed out before
# its own restore ran -- put it back. A no-op otherwise, so this is safe to
# run whether or not the main test needed it.
if(NOT DEFINED PLUGIN)
  message(FATAL_ERROR "restoreplugin.cmake: PLUGIN is required")
endif()

set(hidden "${PLUGIN}.hidden-for-test")
if(EXISTS "${hidden}")
  file(RENAME "${hidden}" "${PLUGIN}")
endif()
