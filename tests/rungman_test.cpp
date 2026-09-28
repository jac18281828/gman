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
 * Drives tests/rungman.h's runGman through /bin/sh -c: gman itself
 * cannot be made to hang, crash or write to one chosen stream on
 * demand, so the contract below runs the same runner against a program
 * that can, covering exit status, the three capture modes, a signal
 * kill, a timeout bounding both read and wait, a working directory, a
 * redirected stdin and an exec failure, each leaving no child behind.
 */

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "check.h"
#include "rungman.h"

namespace {

GMANRunResult runShell(std::string const& script, GMANRunOptions const& options = {}) {
  return runGman("/bin/sh", {"-c", script}, options);
}

void testExitStatus() {
  GMANRunResult const r = runShell("exit 3");
  check(r.exitStatus == 3, "exit status: reports the child's exit code");
  check(!r.crashed, "exit status: does not report a crash");
  check(!r.timedOut, "exit status: does not report a timeout");
}

void testCaptureModes() {
  std::string const script = "echo out; echo err 1>&2; echo out2";

  GMANRunOptions combined;
  combined.capture = GMANRunOptions::Capture::combined;
  check(runShell(script, combined).output == "out\nerr\nout2\n", "combined: keeps the order the child wrote");

  GMANRunOptions stdoutOnly;
  stdoutOnly.capture = GMANRunOptions::Capture::stdoutOnly;
  check(runShell(script, stdoutOnly).output == "out\nout2\n", "stdout only: drops stderr");

  GMANRunOptions stderrOnly;
  stderrOnly.capture = GMANRunOptions::Capture::stderrOnly;
  check(runShell(script, stderrOnly).output == "err\n", "stderr only: drops stdout");
}

void testSignalKill() {
  // SIGTERM, not SIGSEGV: macOS writes a crash report for the latter.
  GMANRunResult const r = runShell("kill -TERM $$");
  check(r.crashed, "signal kill: reports a crash");
  check(r.exitStatus == -1, "signal kill: exit status is -1, not the shell's 128+n");
  check(!r.timedOut, "signal kill: does not report a timeout");
}

void testTimeoutBoundsReadAndWait() {
  GMANRunOptions options;
  options.timeoutSeconds = 1;
  auto const start = std::chrono::steady_clock::now();
  GMANRunResult const r = runShell("exec sleep 10", options);
  auto const elapsed = std::chrono::steady_clock::now() - start;
  check(r.timedOut, "timeout: reports a timeout");
  check(!r.crashed, "timeout: does not report a crash");
  check(r.exitStatus == -1, "timeout: exit status is -1");
  check(elapsed < std::chrono::seconds(5), "timeout: the call itself returns well inside the ctest bound");
}

void testWorkingDirectory() {
  std::filesystem::path const dir = std::filesystem::current_path() / "rungman-workdir";
  std::filesystem::create_directory(dir);
  std::string const canonical = std::filesystem::canonical(dir).string();

  GMANRunOptions options;
  options.workingDirectory = dir.string();
  GMANRunResult const r = runShell("pwd -P", options);
  check(r.output == canonical + "\n", "working directory: the child runs where options asked");
}

void testStdinIsDevNull() {
  // Makes the test's own stdin a pipe an inherited stdin would block on,
  // so this proves the child's stdin is /dev/null rather than whatever the
  // caller happened to hold open.
  int pipeFds[2];
  check(pipe(pipeFds) == 0, "stdin setup: creates the blocking pipe");
  int const savedStdin = dup(STDIN_FILENO);
  dup2(pipeFds[0], STDIN_FILENO);
  close(pipeFds[0]);

  GMANRunOptions options;
  options.timeoutSeconds = 5;
  GMANRunResult const r = runShell("cat", options);

  dup2(savedStdin, STDIN_FILENO);
  close(savedStdin);
  close(pipeFds[1]);

  check(!r.timedOut, "stdin: cat sees EOF instead of the caller's blocking pipe");
  check(r.output.empty(), "stdin: cat reads nothing");
}

void testMissingBinary() {
  GMANRunResult const r = runGman("/no/such/gman", {});
  check(r.exitStatus == 127, "missing binary: a failed exec exits 127");
}

// Checked after every case above: proves that case's own call left no
// child running or unreaped, rather than one final check that a run
// earlier in the file could pass by accident.
void checkNoChildLeftBehind(std::string const& caseName) {
  errno = 0;
  pid_t const r = waitpid(-1, nullptr, WNOHANG);
  check(r == -1 && errno == ECHILD, caseName + ": no child left running or unreaped");
}

} // namespace

int main() {
  testExitStatus();
  checkNoChildLeftBehind("exit status");
  testCaptureModes();
  checkNoChildLeftBehind("capture modes");
  testSignalKill();
  checkNoChildLeftBehind("signal kill");
  testTimeoutBoundsReadAndWait();
  checkNoChildLeftBehind("timeout");
  testWorkingDirectory();
  checkNoChildLeftBehind("working directory");
  testStdinIsDevNull();
  checkNoChildLeftBehind("stdin");
  testMissingBinary();
  checkNoChildLeftBehind("missing binary");

  return checkSummary("runGman's contract holds");
}
