/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * GMANQuantize::doColor's "Color quantization not currently implemented."
 * message is genuine -- colour quantization really is unimplemented -- but
 * every output driver calls it once per pixel with no once-per-process
 * guard, so tests/rib/shaders.rib (600x200) produced 120,000 lines of it.
 * Fixed to log once per process, matching GMANTransform::apply's own
 * static-bool idiom. shaders.rib still reaches the quantizer on every
 * pixel; the message must now appear exactly once in its output, not zero
 * (the fix must not delete the message) and not more than once (the fix
 * must not merely reduce the count).
 */

#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"

namespace {

struct Result {
  int exitStatus;
  std::string output;
};

Result runGman(const std::string &gman, const std::string &rib) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" 2>&1";
  std::FILE *pipe = popen(command.c_str(), "r");
  Result result{-1, ""};
  if (pipe == nullptr) {
    return result;
  }
  char buffer[512];
  while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
    result.output += buffer;
  }
  const int status = pclose(pipe);
  result.exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

int countOccurrences(const std::string &haystack, const std::string &needle) {
  int count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  const std::string rib = ribDir + "/shaders.rib";
  Result r = runGman(gman, rib);
  check(r.exitStatus == 0, "shaders.rib renders");

  const std::string message = "Color quantization not currently implemented.";
  int count = countOccurrences(r.output, message);

  check(count >= 1,
        "quantize: the message still appears for a scene that reaches "
        "the quantizer");
  check(count <= 1,
        "quantize: the message appears at most once per process (found " +
            std::to_string(count) + ")");

  return checkSummary("quantize holds");
}
