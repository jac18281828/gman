/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * The phase 2 runtime baseline.
 *
 * Phase 0 recorded this test's original finding: given tests/rib/sphere.rib,
 * gman exited 1, wrote no image, and reported
 * "ERROR: RIE_CONSISTENCY -- GMANParameterList: TOKEN_NOT_FOUND" --
 * GMANRIBParse::parseParameterList discarded bracketed array parameters
 * outright, so "fov" [45] never reached the projection's parameter list.
 *
 * Phase 2's step 7 fixed that path (see gmanribparse.cpp's
 * parseParameterList and the FIXMEs it superseded). Both "fov" [45] and the
 * unbracketed "fov" 45 now reach the projection correctly, RiWorldBegin
 * loads a renderer, and gman runs the sphere through the zbuffer renderer to
 * completion. Asserted below: exit 0 and a real TIFF file.
 *
 * Phase 2 also fixed a second, unrelated bug this path exposed for the first
 * time: GMANBody::~GMANBody() (libgman/gmanbody.cpp) reassigned its walk
 * pointer to the surface it had just freed instead of to that surface's
 * `next`, so any body with a surface use-after-freed on teardown. Nothing
 * before phase 2 ever got far enough to construct and then destroy a real
 * primitive to hit it.
 *
 * The image is not yet correctly projected, lit or shaded -- the four
 * severed links SPEC.md S2 describes (transform application, face normals,
 * backface culling, the perspective matrix) are all still broken. Phase 1
 * owns the object-to-raster space chain and inverts the second half of this
 * test into a sphere silhouette assertion; phase 3 replaces that with a
 * golden shaded image. Do not "fix" this test to expect a correct image --
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

  check(exitStatus == 0, "gman exits 0");
  check(output.find("Parsing") != std::string::npos,
        "the RIB was opened and parsing started");
  check(output.find("TOKEN_NOT_FOUND") == std::string::npos,
        "the array-parameter bug that pinned exit 1 does not recur");

  // parseParameterList now reaches the projection's "fov" [45], WorldBegin
  // loads the zbuffer renderer, and it runs the sphere through to a real
  // TIFF.
  std::FILE *produced = std::fopen(image, "rb");
  check(produced != nullptr, "an image file is produced");
  if (produced != nullptr) {
    std::fseek(produced, 0, SEEK_END);
    long size = std::ftell(produced);
    check(size > 0, "the image file is not empty");
    std::fclose(produced);
  }

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  std::printf("baseline holds\n");
  return 0;
}
