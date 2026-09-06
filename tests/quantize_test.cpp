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
 *
 * doColor has two overloads, GMANColor& and GMANColorRGB&; every output
 * driver calls only the GMANColorRGB& one, so the GMANColorRGB scenario
 * above is the one a revert would actually surface in. The GMANColor&
 * overload has no live caller, so its own warn-once guard is exercised
 * directly below, in a forked child (to isolate it from the process-wide
 * static the subprocess scenario above already spent): both overloads are
 * called once each and must together log the message exactly once, proving
 * the guard is shared rather than duplicated per overload.
 */

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"
#include "gmancolor.h"
#include "gmanquantize.h"

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

// Forks so the two overloads run in a process of their own, with its own
// copy of quantizeWarned -- otherwise a prior call in this same test binary
// (there is none today, but a future test added to this file could add
// one) would make the "exactly once" assertion pass for the wrong reason.
std::string callBothOverloadsInChild(const std::string &captureFile) {
  pid_t pid = fork();
  if (pid < 0) {
    return "";
  }
  if (pid == 0) {
    FILE *outRedirect = std::freopen(captureFile.c_str(), "w", stdout);
    (void)outRedirect;
    GMANQuantize quantizer(GMANQuantize::RGB, 1, 0, 255, 0.0);
    GMANColor color(static_cast<GMANColorSample>(0.5),
                    static_cast<GMANColorSample>(0.5),
                    static_cast<GMANColorSample>(0.5));
    GMANColorRGB colorRgb;
    colorRgb.setRed(128);
    colorRgb.setGreen(128);
    colorRgb.setBlue(128);
    quantizer.doColor(color);
    quantizer.doColor(colorRgb);
    std::fflush(stdout);
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);

  std::string output;
  std::FILE *file = std::fopen(captureFile.c_str(), "r");
  if (file == nullptr) {
    return output;
  }
  char buffer[512];
  while (std::fgets(buffer, sizeof buffer, file) != nullptr) {
    output += buffer;
  }
  std::fclose(file);
  return output;
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

  // Relative to WORKING_DIRECTORY -- a scratch dir CMake creates for this
  // test, not the read-only tests/rib source tree ribDir names.
  const std::string captureFile = "quantize-overload-capture.txt";
  const std::string overloadOutput = callBothOverloadsInChild(captureFile);
  std::remove(captureFile.c_str());
  const int overloadCount = countOccurrences(overloadOutput, message);
  check(overloadCount == 1,
        "quantize: GMANColor& and GMANColorRGB& share one warn-once guard "
        "(found " +
            std::to_string(overloadCount) + ")");

  return checkSummary("quantize holds");
}
