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
 * RowProgress logs one line for each whole percent of rows finished, however
 * many rows finish between two percents and however few rows there are.
 */

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "gmanlog.h"
#include "gmanrenderprogress.h"

namespace {

const std::string kName = "gmanrows";
const std::string kNamePrefix = kName + ": ";

std::vector<int> percentsLogged(RtInt total) {
  const std::string path = "renderprogressrows_" + std::to_string(total) + ".log";
  std::remove(path.c_str());
  setLogFile(path.c_str());
  setScreenOutput(false);

  gman::RowProgress progress(kName, total);
  for (RtInt done = 1; done <= total; ++done) {
    progress(done);
  }

  setLogFile("/dev/null");
  setScreenOutput(true);

  std::ifstream in(path);
  std::vector<int> percents;
  std::string line;
  while (std::getline(in, line)) {
    const auto at = line.find(kNamePrefix);
    if (at == std::string::npos) {
      continue;
    }
    percents.push_back(std::stoi(line.substr(at + kNamePrefix.size())));
  }
  return percents;
}

std::vector<int> everyPercent() {
  std::vector<int> percents;
  for (int percent = 1; percent <= 100; ++percent) {
    percents.push_back(percent);
  }
  return percents;
}

std::vector<int> wholePercentsOf(RtInt total) {
  std::vector<int> percents;
  for (RtInt done = 1; done <= total; ++done) {
    percents.push_back(done * 100 / total);
  }
  return percents;
}

void testManyRowsLogEachPercentOnce() {
  check(percentsLogged(250) == everyPercent(), "250 rows log exactly one line for each percent from 1 to 100");
}

void testFewRowsLogEachRow() {
  check(percentsLogged(48) == wholePercentsOf(48), "48 rows log one line per row, at that row's whole percent");
}

void testOneRowLogsOnlyTheLastLine() {
  check(percentsLogged(1) == std::vector<int>{100}, "one row logs a single line at 100%");
}

} // namespace

int main() {
  testManyRowsLogEachPercentOnce();
  testFewRowsLogEachRow();
  testOneRowLogsOnlyTheLastLine();

  return checkSummary("RowProgress logs one line for each whole percent");
}
