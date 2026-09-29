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
 * Step 4: the RIB tokenizer on malformed input it has not already been
 * exercised against -- an unterminated string, an unbalanced bracketed
 * array, and a file truncated mid-keyword with no trailing newline.
 * tests/ribdialect_test.cpp already covers two malformed shapes (a
 * non-string array element, a non-string Display argument) that are
 * type errors, not tokenizer-level ones.
 *
 * string_trailing_backslash.rib pins the same end-of-input bar for the
 * escape decoding parseString gained afterward: a backslash with nothing
 * after it is an incomplete escape, not a literal character, and must stop
 * the tokenizer as cleanly as the plain unterminated string above.
 *
 * declare_array_overflow.rib is not a tokenizer defect either -- it is a
 * Declare array-size digit string that overflows even `long`. It shares
 * this harness anyway: gman::InlineParse::is_int gating that size is the
 * only thing standing between a malformed RiDeclare and an uncaught
 * std::bad_alloc sizing GMANParameterList's allocation, so "does not
 * crash" is exactly this file's own bar.
 *
 * The requirement is narrower than "parses correctly": a clean, bounded
 * exit -- crash or hang either would defeat every other test's own
 * process-spawning assumption. Each fixture runs under a hard wall-clock
 * timeout of its own (tests/rungman.h), not ctest's own TIMEOUT
 * property, which bounds the whole binary, not one fixture.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"
#include "rungman.h"

namespace {

GMANRunResult runWithTimeout(std::string const& gman, std::string const& rib, int timeoutSeconds) {
  GMANRunOptions options;
  options.timeoutSeconds = timeoutSeconds;
  return runGman(gman, {rib}, options);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib/malformed dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  const char* fixtures[] = {
      "unterminated_string.rib",    "unbalanced_bracket.rib",        "truncated.rib",
      "declare_array_overflow.rib", "string_trailing_backslash.rib",
  };

  for (const char* fixture : fixtures) {
    const std::string rib = dir + "/" + fixture;
    GMANRunResult r = runWithTimeout(gman, rib, 10);
    check(!r.timedOut, std::string(fixture) + ": does not hang (10s bound)");
    check(!r.crashed, std::string(fixture) + ": does not crash (no signal termination)");
  }

  return checkSummary("RIB malformed-input handling holds");
}
