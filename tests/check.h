/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */
/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */

/*
 * The harness every hand-rolled C++ test in this tree shares: a pass/fail
 * counter and a summary line. Extracted from twenty near-identical copies
 * (see AGENTS.md's Tests section for the one way to add a test).
 *
 * tests/basicstate.c is exempt: it is a genuine C translation unit
 * exercising the extern "C" RI API, with its own static void check(int,
 * const char *) matching that language's calling convention.
 */

#ifndef GMAN_TESTS_CHECK_H
#define GMAN_TESTS_CHECK_H

#include <cstdio>
#include <string>

inline int failures = 0;

inline void check(bool ok, const std::string &what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

// Prints the pass/fail summary and returns the test binary's exit code.
inline int checkSummary(const char *okMessage) {
  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("%s\n", okMessage);
  return 0;
}

#endif
