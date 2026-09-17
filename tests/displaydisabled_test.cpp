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
 * A known but disabled file driver must fail loudly: gmanMakeFileOutput
 * throws RIE_BADFILE naming the extension and the missing library. Which
 * drivers are disabled is a runtime fact of this build, read back from
 * gman --version, rather than a compile-time #ifdef in this test -- the
 * same binary runs unchanged across the ON, OFF and mixed configurations.
 * With both drivers compiled there is nothing disabled to exercise, so
 * this test exits 77 (SKIP_RETURN_CODE); the drivers-off CI job is where
 * it actually renders.
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "check.h"

namespace {

struct CapturedRun {
  int exitStatus = -1;
  std::string stdoutText;
};

// Spawns argv via fork/exec/waitpid, the technique
// tests/ribmalformed_test.cpp uses, capturing stdout -- GMANHandleError
// (gmanerror.cpp's print()) writes every diagnostic there. Output here is
// a handful of short lines, well under a pipe's buffer, so reading it
// after the child exits cannot deadlock.
CapturedRun runCaptured(std::vector<std::string> const& argv) {
  CapturedRun result;
  int outPipe[2];
  if (pipe(outPipe) != 0) {
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(outPipe[0]);
    close(outPipe[1]);
    return result;
  }
  if (pid == 0) {
    close(outPipe[0]);
    dup2(outPipe[1], STDOUT_FILENO);
    close(outPipe[1]);
    std::vector<char*> cargv;
    for (auto const& arg : argv) {
      cargv.push_back(const_cast<char*>(arg.c_str()));
    }
    cargv.push_back(nullptr);
    execv(cargv[0], cargv.data());
    _exit(127);
  }

  close(outPipe[1]);
  char buffer[4096];
  ssize_t bytesRead;
  while ((bytesRead = read(outPipe[0], buffer, sizeof buffer)) > 0) {
    result.stdoutText.append(buffer, size_t(bytesRead));
  }
  close(outPipe[0]);

  int status = 0;
  waitpid(pid, &status, 0);
  result.exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

// True when `name` appears as one of the space-separated tokens on
// --version's "drivers:" line.
bool driverCompiled(std::string const& versionStdout, std::string const& name) {
  std::istringstream lines(versionStdout);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.rfind("drivers:", 0) != 0) {
      continue;
    }
    std::istringstream tokens(line.substr(std::string("drivers:").size()));
    std::string token;
    while (tokens >> token) {
      if (token == name) {
        return true;
      }
    }
  }
  return false;
}

bool fileExists(std::string const& path) {
  std::ifstream in(path);
  return in.good();
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  const auto version = runCaptured({gman, "--version"});
  check(version.exitStatus == 0, "gman --version exits 0");

  const auto pngCompiled = driverCompiled(version.stdoutText, "png");
  const auto jpegCompiled = driverCompiled(version.stdoutText, "jpeg");

  if (pngCompiled && jpegCompiled) {
    // Nothing disabled to exercise in this build -- drivers-off runs it
    // for real.
    return 77;
  }

  if (!pngCompiled) {
    const std::string output = "x.png";
    std::remove(output.c_str());
    const auto run = runCaptured({gman, ribDir + "/displaydisabled_png.rib"});
    check(run.exitStatus != 0, "a disabled PNG driver fails the render");
    check(run.stdoutText.find("Display \"png\": built without libpng") != std::string::npos,
          "the failure names the disabled png driver");
    check(!fileExists(output), "a disabled PNG driver writes no file");
  }

  if (!jpegCompiled) {
    const std::string output = "x.jpg";
    std::remove(output.c_str());
    const auto run = runCaptured({gman, ribDir + "/displaydisabled_jpg.rib"});
    check(run.exitStatus != 0, "a disabled JPEG driver fails the render");
    check(run.stdoutText.find("Display \"jpg\": built without libjpeg") != std::string::npos,
          "the failure names the disabled jpeg driver");
    check(!fileExists(output), "a disabled JPEG driver writes no file");
  }

  return checkSummary("disabled file drivers fail loudly");
}
