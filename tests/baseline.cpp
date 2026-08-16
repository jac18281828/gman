/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * The phase 0 runtime baseline.
 *
 * Nothing in this repository had been compiled on a modern machine before
 * phase 0, so what GMAN does at runtime was an assumption. This test records
 * what it actually does, so that a later phase can tell a deliberate change
 * from a regression.
 *
 * Observed, and asserted below: given tests/rib/sphere.rib, gman exits 1,
 * writes no image, and reports
 *
 *     ERROR: RIE_CONSISTENCY -- GMANParameterList: TOKEN_NOT_FOUND
 *
 * Why: GMANRIBParse::parseParameterList discards bracketed array parameters
 * outright -- the LEFT_BRACKET branch parses the array and drops it under a
 * FIXME. So "fov" [45] never reaches the projection's parameter list, and
 * RiWorldBegin's lookup of RI_FOV throws out of
 * GMANParameterList::getPointer, which throws rather than returning NULL as
 * its caller expects. The failure happens before any renderer runs, which is
 * why no file appears.
 *
 * The prediction this replaces was an entirely black 640x480 TIFF. That was
 * traced from source and never run; it is wrong.
 *
 * SUPERSEDED BY PHASE 1. Phase 1 owns the object-to-raster space chain and
 * inverts this test into a sphere silhouette assertion. Phase 3 replaces that
 * with a golden shaded image. Do not "fix" this test to expect success --
 * replace it when the behavior it pins is deliberately changed.
 */

#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string &what)
{
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

} // namespace

int main(int argc, char *argv[])
{
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <sphere.rib>\n", argv[0]);
    return 2;
  }

  const std::string gman = argv[1];
  const std::string rib = argv[2];
  const char *image = "sphere.tif";

  // CTest runs this in a scratch directory of its own, so the image the RIB
  // asks for would land beside us if it were ever written.
  std::remove(image);

  const std::string command = "\"" + gman + "\" \"" + rib + "\" 2>&1";

  std::FILE *pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::fprintf(stderr, "FAIL: could not run %s\n", command.c_str());
    return 1;
  }

  std::string output;
  char buffer[512];
  while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
    output += buffer;
  }

  const int closeStatus = pclose(pipe);
  const int exitStatus = WIFEXITED(closeStatus) ? WEXITSTATUS(closeStatus) : -1;

  std::printf("--- gman output ---\n%s-------------------\n", output.c_str());

  check(exitStatus == 1, "gman exits 1");
  check(output.find("GMANParameterList: TOKEN_NOT_FOUND") != std::string::npos,
        "reports GMANParameterList: TOKEN_NOT_FOUND");
  check(output.find("RIE_CONSISTENCY") != std::string::npos,
        "the error is RIE_CONSISTENCY");
  check(output.find("Parsing") != std::string::npos,
        "the RIB was opened and parsing started");

  // The failure is raised from RiWorldBegin, after the display driver has
  // been selected but before any pixel is written.
  std::FILE *produced = std::fopen(image, "rb");
  check(produced == nullptr, "no image file is produced");
  if (produced != nullptr) {
    std::fclose(produced);
  }

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  std::printf("baseline holds\n");
  return 0;
}
