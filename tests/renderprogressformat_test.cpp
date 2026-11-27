/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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
 * The progress line and its durations, as pure functions of the row counts
 * and the elapsed time, and the option that asks for the line.
 */

#include <chrono>
#include <string>
#include <vector>

#include "check.h"
#include "gmanoptions.h"
#include "gmanrenderprogress.h"

namespace {

void testDurations() {
  check(gman::formatDuration(std::chrono::seconds{42}) == "42s", "42 seconds read 42s");
  check(gman::formatDuration(std::chrono::seconds{185}) == "3m05s", "185 seconds read 3m05s");
  check(gman::formatDuration(std::chrono::seconds{4320}) == "1h12m", "4320 seconds read 1h12m");
  check(gman::formatDuration(std::chrono::seconds{3600 + 300}) == "1h05m", "an hour and five minutes read 1h05m");
  check(gman::formatDuration(std::chrono::seconds{0}) == "0s", "no time reads 0s");
}

void testLines() {
  check(gman::progressLine("gmanpathtracer", 1432, 3072, std::chrono::seconds{4320}) ==
            "gmanpathtracer: 46% (1432/3072 rows), 1h12m elapsed, ~1h22m left",
        "a line in progress names its percent, counts, elapsed time and time left");
  check(gman::progressLine("gmanpathtracer", 3072, 3072, std::chrono::seconds{4320}) ==
            "gmanpathtracer: 100% (3072/3072 rows), 1h12m elapsed",
        "the 100% line has no time left");
  check(gman::progressLine("gmanraytracer", 24, 48, std::chrono::seconds{10}) ==
            "gmanraytracer: 50% (24/48 rows), 10s elapsed, ~10s left",
        "the time left scales elapsed by the rows remaining over the rows done");
}

void testOption() {
  GMANOptions options;
  check(!gman::progressRequested(options), "absent, progress is off");

  options.setNamedOption("statistics", "progress", std::vector<RtInt>{0});
  check(!gman::progressRequested(options), "0 turns progress off");

  options.setNamedOption("statistics", "progress", std::vector<RtInt>{1});
  check(gman::progressRequested(options), "1 turns progress on");

  options.setNamedOption("statistics", "progress", std::vector<RtInt>{2});
  check(gman::progressRequested(options), "any nonzero value turns progress on");

  options.setNamedOption("statistics", "progress", std::vector<std::string>{"yes"});
  check(!gman::progressRequested(options), "a string value leaves progress off");
}

} // namespace

int main() {
  testDurations();
  testLines();
  testOption();

  return checkSummary("the progress line and its option hold their contract");
}
