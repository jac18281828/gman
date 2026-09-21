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
 * GMANRIBTokenize::parseString used to treat a backslash as an ordinary
 * character, so the first '"' inside a string -- escaped or not -- closed
 * it. tests/rib/string_escape.rib is the desync this produced: an escaped
 * quote inside a bracketed array closed the string early, the array never
 * saw its ']', and the parse ran on against the wrong quoting from there.
 *
 * Check 1 drives that fixture two ways: out of process, following
 * tests/paramlistdeclaredtype_test.cpp's runCapturingOutput, to confirm the
 * file parses to completion with no "unterminated array" anywhere in its
 * output; and in process, through a GMANRenderManImpl subclass overriding
 * RiAttributeV, to read the decoded parameter value itself back out during
 * the call -- parseParameterList's buffer is freed once the Attribute
 * request's own parseStream iteration ends, so the pointer is read before
 * the override returns, not after.
 *
 * Checks 2 through 4b drive GMANRIBTokenize::getNext directly on a
 * std::istringstream, reading each result with getString(). parseString
 * itself is private and stays private; getNext is the tokenizer's public
 * entry point and include/gmanribtokenize.h is on gman_core's PUBLIC
 * include path, so this needs no internal header and no test-only access
 * widening. getRtToken() is never used here: it returns a pointer that ends
 * at the first NUL, which would hide exactly the byte-level decoding bugs
 * these checks exist to catch.
 */

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "check.h"
#include "gmanrendermanimpl.h"
#include "gmanribparse.h"
#include "gmanribtokenize.h"
#include "ri.h"

namespace {

struct RunResult {
  bool timedOut = false;
  bool crashed = false; // terminated by a signal (SIGSEGV, SIGABRT, ...)
  int exitStatus = -1;
  std::string output;
};

// See tests/paramlistdeclaredtype_test.cpp's own runCapturingOutput: polls
// rather than SIGALRM, captures the child's combined stdout/stderr.
RunResult runCapturingOutput(const std::string& gman, const std::string& rib, int timeoutSeconds) {
  RunResult result;

  int pipeFds[2];
  if (pipe(pipeFds) != 0) {
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipeFds[0]);
    close(pipeFds[1]);
    return result;
  }
  if (pid == 0) {
    close(pipeFds[0]);
    dup2(pipeFds[1], STDOUT_FILENO);
    dup2(pipeFds[1], STDERR_FILENO);
    close(pipeFds[1]);
    execl(gman.c_str(), gman.c_str(), rib.c_str(), (char*)nullptr);
    _exit(127);
  }
  close(pipeFds[1]);

  char buf[4096];
  ssize_t n;
  while ((n = read(pipeFds[0], buf, sizeof(buf))) > 0) {
    result.output.append(buf, (size_t)n);
  }
  close(pipeFds[0]);

  const int pollIntervalUs = 50 * 1000;
  const int maxPolls = (timeoutSeconds * 1000000) / pollIntervalUs;
  int status = 0;
  for (int i = 0; i < maxPolls; ++i) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      if (WIFEXITED(status)) {
        result.exitStatus = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        result.crashed = true;
      }
      return result;
    }
    usleep(pollIntervalUs);
  }

  result.timedOut = true;
  kill(pid, SIGKILL);
  waitpid(pid, &status, 0);
  return result;
}

// Records what RiAttributeV actually received for "note", without keeping
// the pointer itself: parseParameterList's buffer is freed once the
// Attribute request's own parseStream iteration ends, so the value is read
// out during the call, not after it returns.
class CapturingRenderMan : public GMANRenderManImpl {
public:
  bool found = false;
  std::string capturedNote;

  RtVoid RiAttributeV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) override {
    for (RtInt i = 0; i < n; ++i) {
      if (std::string(tokens[i]) == "note") {
        found = true;
        RtToken* values = (RtToken*)parms[i];
        capturedNote = std::string(values[0]);
      }
    }
    GMANRenderManImpl::RiAttributeV(name, n, tokens, parms);
  }
};

// Decodes one RIB string token through the tokenizer's public entry point.
std::string decode(GMANRIBTokenize& tokenizer, const std::string& ribText) {
  std::istringstream stream(ribText);
  return tokenizer.getNext(stream).getString();
}

// Runs fn with the current process's own stdout redirected to a pipe, and
// returns what it wrote. Used by check 4 to confirm an unknown escape
// prints no diagnostic, the same bar checks 1-4 of
// tests/paramlistdeclaredtype_test.cpp hold gman's own process output to.
std::string captureStdout(void (*fn)(GMANRIBTokenize&, const std::string&), GMANRIBTokenize& tokenizer,
                          const std::string& ribText) {
  std::fflush(stdout);
  int savedStdout = dup(STDOUT_FILENO);
  int pipeFds[2];
  if (pipe(pipeFds) != 0) {
    close(savedStdout);
    return std::string();
  }
  dup2(pipeFds[1], STDOUT_FILENO);
  close(pipeFds[1]);

  fn(tokenizer, ribText);

  std::fflush(stdout);
  dup2(savedStdout, STDOUT_FILENO);
  close(savedStdout);

  fcntl(pipeFds[0], F_SETFL, O_NONBLOCK);
  std::string output;
  char buf[4096];
  ssize_t n;
  while ((n = read(pipeFds[0], buf, sizeof(buf))) > 0) {
    output.append(buf, (size_t)n);
  }
  close(pipeFds[0]);
  return output;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string dir = argv[2];

  // 1. The desync itself is gone: an escaped quote inside a bracketed
  // array no longer closes the string early.
  {
    RunResult r = runCapturingOutput(gman, dir + "/string_escape.rib", 10);
    check(!r.timedOut, "string_escape: does not hang (10s bound)");
    check(!r.crashed, "string_escape: does not crash");
    check(r.exitStatus == EXIT_SUCCESS, "string_escape: exits cleanly");
    check(r.output.find("unterminated array") == std::string::npos,
          "string_escape: no \"unterminated array\" anywhere in the output");
  }
  {
    CapturingRenderMan renderMan;
    const std::string rib = dir + "/string_escape.rib";
    {
      GMANRIBParse parser(renderMan, rib.c_str());
      parser.parse();
    }
    check(renderMan.found, "string_escape: \"note\" reaches RiAttributeV");
    check(renderMan.capturedNote == "a \" b", "string_escape: decodes to `a \" b`, a literal quote and all");
  }

  // 2. Each row of the escape table decodes to its stated byte(s).
  {
    GMANRIBTokenize tokenizer;
    check(decode(tokenizer, "\"\\n\"") == "\n", "table: \\n decodes to newline");
    check(decode(tokenizer, "\"\\r\"") == "\r", "table: \\r decodes to carriage return");
    check(decode(tokenizer, "\"\\t\"") == "\t", "table: \\t decodes to tab");
    check(decode(tokenizer, "\"\\b\"") == "\b", "table: \\b decodes to backspace");
    check(decode(tokenizer, "\"\\f\"") == "\f", "table: \\f decodes to form feed");
    check(decode(tokenizer, "\"\\\\\"") == "\\", "table: \\\\ decodes to one backslash");
    check(decode(tokenizer, "\"\\\"\"") == "\"", "table: \\\" decodes to one double quote");
    check(decode(tokenizer, "\"\\101\"") == "A", "table: \\101 decodes to 'A'");
    check(decode(tokenizer, "\"line\\\ncont\"") == "linecont",
          "table: a backslash before a literal newline continues the string");
  }

  // 3. The octal grammar stops where the table says: one to three digits,
  // ending at the first non-octal character or the third digit, whichever
  // comes first.
  {
    GMANRIBTokenize tokenizer;
    check(decode(tokenizer, "\"\\101\"") == "A", "octal: \\101 is 'A'");
    check(decode(tokenizer, "\"\\1013\"") == "A3", "octal: \\1013 is 'A' then a literal '3'");
    check(decode(tokenizer, "\"\\129\"") == "\n9", "octal: \\12 ends at the non-octal '9' that follows it");
    check(decode(tokenizer, "\"\\8\"") == "8", "octal: \\8 is an unknown escape, not an octal one, and yields '8'");
  }

  // 4. An unknown escape drops the backslash silently: the decoded
  // character, and no diagnostic on the way.
  {
    GMANRIBTokenize tokenizer;
    std::string captured = captureStdout(
        [](GMANRIBTokenize& t, const std::string& text) {
          std::istringstream stream(text);
          t.getNext(stream);
        },
        tokenizer, "\"\\q\"");
    check(captured.empty(), "unknown escape: \\q emits no diagnostic");
    check(decode(tokenizer, "\"\\q\"") == "q", "unknown escape: \\q decodes to 'q'");
  }

  // 4b. A truncated octal escape at end of input is clean: no throw, no
  // hang, no read past the end -- exactly like a bare trailing backslash.
  {
    GMANRIBTokenize tokenizer;
    bool threw = false;
    try {
      std::istringstream stream("\"\\1");
      tokenizer.getNext(stream);
    } catch (...) {
      threw = true;
    }
    check(!threw, "truncated octal: \"\\1 at end of input does not throw");

    threw = false;
    try {
      std::istringstream stream("\"\\10");
      tokenizer.getNext(stream);
    } catch (...) {
      threw = true;
    }
    check(!threw, "truncated octal: \"\\10 at end of input does not throw");
  }

  return checkSummary("RIB strings decode their escape sequences");
}
