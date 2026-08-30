/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
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
