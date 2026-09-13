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
 * gmanlog's sink, exercised through its public API only. setLogFile used to
 * hold logMutex across a call to info(), which locks the same mutex again --
 * a hang, not a crash, so nothing before this test caught it. Every check
 * here runs against the fixed sink; the file is the one place gmanlog's own
 * concurrency invariant (one mutex, taken only inside logEnabled/logWrite)
 * is proven from outside.
 */

#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "check.h"
#include "gmanlog.h"

namespace {

std::string readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

// setLogFile opens its target with "a", so a file left over from a
// previous run of this binary in the same WORKING_DIRECTORY would carry
// stale lines into a fresh count. Each test starts from no file at all.
void removeIfExists(const std::string &path) {
  std::remove(path.c_str());
}

// A value whose formatter counts every format() call, to prove a message
// below the current level is never formatted.
int gFormatCalls = 0;

struct CountedValue {
  int value;
};

}  // namespace

template <>
struct std::formatter<CountedValue> : std::formatter<int> {
  auto format(const CountedValue &v, std::format_context &ctx) const {
    ++gFormatCalls;
    return std::formatter<int>::format(v.value, ctx);
  }
};

namespace {

// setLogFile must return, not hang, and the message a following call logs
// must land in the file it names.
void testNoDeadlock() {
  setLogLevel(LOGLVL_DEBUG);
  setScreenOutput(false);
  removeIfExists("log_basic.log");
  setLogFile("log_basic.log");
  warning("{} {}", "alpha", 42);

  const std::string contents = readFile("log_basic.log");
  check(contents.find("GMAN WARNING: alpha 42") != std::string::npos,
        "setLogFile returns and a following warning lands in the file");
}

// With screen output on and stdout redirected to a file, one message must
// appear whole in both the log file and the redirected stdout.
void testBothOutputsSeeMessage() {
  setLogLevel(LOGLVL_DEBUG);
  removeIfExists("log_both.log");
  setLogFile("log_both.log");
  setScreenOutput(true);

  std::fflush(stdout);
  int savedStdout = dup(fileno(stdout));
  FILE *redirected = std::freopen("log_both.screen", "w", stdout);
  check(redirected != nullptr, "stdout redirected to a file");

  info("both outputs see this line");

  std::fflush(stdout);
  dup2(savedStdout, fileno(stdout));
  close(savedStdout);
  setScreenOutput(false);

  const std::string logContents = readFile("log_both.log");
  const std::string screenContents = readFile("log_both.screen");
  check(logContents.find("both outputs see this line") != std::string::npos,
        "the log file sees the message");
  check(screenContents.find("both outputs see this line") != std::string::npos,
        "stdout sees the message");
}

// A message ending in its own newline gets exactly one trailing newline,
// never two.
void testOneNewline() {
  setLogLevel(LOGLVL_DEBUG);
  setScreenOutput(false);
  removeIfExists("log_newline.log");
  setLogFile("log_newline.log");
  info("line\n");

  const std::string contents = readFile("log_newline.log");
  check(contents.find("GMAN INFO: line\n\n") == std::string::npos,
        "info(\"line\\n\") leaves no blank line after it");
  check(contents.find("GMAN INFO: line\n") != std::string::npos,
        "info(\"line\\n\") lands with exactly one trailing newline");
}

// A message below the current level must never reach std::format.
void testSuppressedLevelsNeverFormat() {
  removeIfExists("log_suppressed.log");
  setLogFile("log_suppressed.log");
  setLogLevel(LOGLVL_ERROR);
  setScreenOutput(false);

  gFormatCalls = 0;
  debug("{}", CountedValue{7});
  check(gFormatCalls == 0,
        "debug() below the current level never formats its argument");
}

// Eight threads each log 1000 numbered lines through the one shared sink;
// the file must hold exactly 8000 well-formed lines -- none interleaved,
// none merged.
void testConcurrentLinesStayWhole() {
  removeIfExists("log_concurrent.log");
  setLogFile("log_concurrent.log");
  setLogLevel(LOGLVL_DEBUG);
  setScreenOutput(false);

  constexpr int kThreads = 8;
  constexpr int kPerThread = 1000;

  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([t] {
      for (int i = 0; i < kPerThread; ++i) {
        warning("worker {} line {}", t, i);
      }
    });
  }
  for (auto &th : threads) {
    th.join();
  }

  const std::string contents = readFile("log_concurrent.log");
  std::istringstream lines(contents);
  std::string line;
  int total = 0;
  int malformed = 0;
  while (std::getline(lines, line)) {
    ++total;
    int worker = -1;
    int index = -1;
    if (std::sscanf(line.c_str(), "GMAN WARNING: worker %d line %d", &worker,
                    &index) != 2) {
      ++malformed;
    }
  }
  check(total == kThreads * kPerThread, "the file holds exactly 8000 lines");
  check(malformed == 0, "every line is well-formed; none interleaved");
}

}  // namespace

int main() {
  testNoDeadlock();
  testBothOutputsSeeMessage();
  testOneNewline();
  testSuppressedLevelsNeverFormat();
  testConcurrentLinesStayWhole();

  return checkSummary(
      "gmanlog holds: no deadlock, both outputs, one "
      "newline, suppressed levels, concurrent writers");
}
