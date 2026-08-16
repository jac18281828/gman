/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * The phase 1 runtime baseline.
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
 * completion.
 *
 * Phase 2 also fixed a second, unrelated bug this path exposed for the first
 * time: GMANBody::~GMANBody() (libgman/gmanbody.cpp) reassigned its walk
 * pointer to the surface it had just freed instead of to that surface's
 * `next`, so any body with a surface use-after-freed on teardown. Nothing
 * before phase 2 ever got far enough to construct and then destroy a real
 * primitive to hit it.
 *
 * Phase 1 inverts the second half of this test again. Through phase 2, the
 * image was uniform -- the four severed links SPEC.md S2 describes meant
 * every face was culled and nothing was ever drawn. Phase 1 connects the
 * object -> world -> camera -> screen -> NDC -> raster chain (see
 * tests/spacechain_test.cpp and tests/silhouette_test.cpp for the numeric
 * proof), so this is where a real, non-uniform image legitimately starts
 * appearing: this test now asserts the TIFF has more than one distinct
 * pixel value, not just that it exists and is non-empty. Do not "fix" this
 * test back to a uniform-image expectation -- replace it when phase 3's
 * shading changes what pixels this pins.
 */

#include <sys/wait.h>

#include <tiffio.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string &what)
{
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

// Distinct pixel values in the produced image -- capped at 2, since this
// only needs to distinguish "uniform" from "not uniform".
int distinctPixelValues(const char *path)
{
  TIFF *tif = TIFFOpen(path, "r");
  if (tif == nullptr) {
    return -1;
  }
  uint32_t width = 0, height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

  std::vector<uint32_t> raster(width * height);
  bool ok = TIFFReadRGBAImageOriented(tif, width, height, raster.data(),
                                       ORIENTATION_TOPLEFT, 0);
  TIFFClose(tif);
  if (!ok || raster.empty()) {
    return -1;
  }

  const uint32_t first = raster[0];
  for (uint32_t p : raster) {
    if (p != first) {
      return 2;
    }
  }
  return 1;
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

  // Phase 1: the coordinate-space chain is connected, so the sphere is no
  // longer culled out of existence -- the image has more than one distinct
  // pixel value. (tests/silhouette_test.cpp and tests/spacechain_test.cpp
  // pin the exact geometry; this only pins that *something* is drawn.)
  int distinct = distinctPixelValues(image);
  check(distinct >= 0, "the produced TIFF can be read back");
  check(distinct > 1,
        "the image is not uniform -- the sphere silhouette is visible");

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  std::printf("baseline holds\n");
  return 0;
}
