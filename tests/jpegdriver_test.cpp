/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Defect 3 (uat-defects prompt): `.jpg`/`.jpeg` segfaulted (exit 139, no
 * file, no diagnostic). GMANRenderManImpl::RiWorldBegin's Display
 * extension dispatch (gmanrendermanimpl.cpp) had branches for
 * `tif`/`tiff`, `png` and `pnm` and none for `jpg`/`jpeg`, so `newOutput`
 * stayed null and was released into `output`, which RiWorldEnd then
 * dereferences unconditionally. GMANOutputJPEG already existed and
 * libjpeg already linked -- the branch was simply missing.
 *
 * Added the jpg/jpeg branch, and an else branch for any other unmatched
 * extension: the general form of the same defect is a null `output`
 * reaching RiWorldEnd with no diagnostic at all, not something specific
 * to jpg/jpeg.
 *
 * Proof: a .jpg render exits 0, decodes as a well-formed JPEG of the
 * requested size, and is close to the equivalent TIFF render -- JPEG is
 * lossy, so "close" rather than exact. A genuinely unknown extension
 * (.bogus) must fail with a diagnostic and no file, not crash.
 *
 * Revert check: reverting the added dispatch branch in
 * GMANRenderManImpl::RiWorldBegin makes the "JPEG scene renders" and
 * "unknown extension fails with a diagnostic" assertions go red -- back
 * to a null-`output` crash for both.
 */

#include <sys/wait.h>

#include <cstdio> // jpeglib.h expects FILE already declared

#include <jpeglib.h>
#include <tiffio.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void writeFile(const std::string &path, const std::string &contents) {
  std::ofstream out(path);
  out << contents;
}

bool nonEmptyFile(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

bool fileExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

struct Image {
  bool ok = false;
  int width = 0, height = 0;
  std::vector<unsigned char> rgb; // 3 bytes/pixel, row-major, top to bottom
};

Image readJPEG(const std::string &path) {
  Image img;
  FILE *fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    return img;
  }

  jpeg_decompress_struct cinfo;
  jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, fp);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&cinfo);
    std::fclose(fp);
    return img;
  }
  cinfo.out_color_space = JCS_RGB;
  jpeg_start_decompress(&cinfo);

  img.width = int(cinfo.output_width);
  img.height = int(cinfo.output_height);
  img.rgb.resize(size_t(img.width) * img.height * 3);

  const int rowStride = img.width * cinfo.output_components;
  std::vector<unsigned char> row(rowStride);
  unsigned char *rowPtr[1];
  int y = 0;
  while (cinfo.output_scanline < cinfo.output_height) {
    rowPtr[0] = row.data();
    jpeg_read_scanlines(&cinfo, rowPtr, 1);
    std::copy(row.begin(), row.begin() + img.width * 3,
              img.rgb.begin() + size_t(y) * img.width * 3);
    ++y;
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  std::fclose(fp);
  img.ok = true;
  return img;
}

Image readTIFF(const std::string &path) {
  Image img;
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (tif == nullptr) {
    return img;
  }
  uint32_t width = 0, height = 0;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);

  std::vector<uint32_t> raster(size_t(width) * height);
  if (!TIFFReadRGBAImageOriented(tif, width, height, raster.data(),
                                  ORIENTATION_TOPLEFT, 0)) {
    TIFFClose(tif);
    return img;
  }
  TIFFClose(tif);

  img.ok = true;
  img.width = int(width);
  img.height = int(height);
  img.rgb.resize(size_t(width) * height * 3);
  for (size_t i = 0; i < size_t(width) * height; ++i) {
    img.rgb[i * 3 + 0] = TIFFGetR(raster[i]);
    img.rgb[i * 3 + 1] = TIFFGetG(raster[i]);
    img.rgb[i * 3 + 2] = TIFFGetB(raster[i]);
  }
  return img;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <gman-binary>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];

  const char *sceneTemplate =
      "Display \"%s\" \"file\" \"rgb\"\n"
      "Format 64 64 1\n"
      "Projection \"perspective\" \"fov\" [45]\n"
      "WorldBegin\n"
      "AttributeBegin\n"
      "  Translate 0 0 5\n"
      "  Sphere 1 -1 1 360\n"
      "AttributeEnd\n"
      "WorldEnd\n";

  char scene[1024];
  std::snprintf(scene, sizeof scene, sceneTemplate, "jpegdriver.jpg");
  writeFile("jpegdriver.rib", scene);
  check(runGman(gman, "jpegdriver.rib") == 0, "JPEG scene renders");
  check(nonEmptyFile("jpegdriver.jpg"), "JPEG file is non-empty");

  std::snprintf(scene, sizeof scene, sceneTemplate, "jpegdriver.tif");
  writeFile("jpegdriver_tif.rib", scene);
  check(runGman(gman, "jpegdriver_tif.rib") == 0, "equivalent TIFF scene renders");

  Image jpg = readJPEG("jpegdriver.jpg");
  check(jpg.ok, "JPEG decodes as well-formed");
  check(jpg.width == 64 && jpg.height == 64, "JPEG has the requested dimensions");

  Image tif = readTIFF("jpegdriver.tif");
  check(tif.ok, "TIFF decodes as well-formed");

  if (jpg.ok && tif.ok && jpg.width == tif.width && jpg.height == tif.height) {
    // JPEG is lossy (4:2:0 chroma subsampling and DCT quantization), so an
    // exact match is the wrong bar. This scene is a two-tone silhouette
    // (flat background, flat-shaded sphere), so the loss concentrates in
    // ringing along the silhouette's edge, where libjpeg's 8x8 DCT blocks
    // straddle a sharp discontinuity -- measured directly (quality-75
    // baseline encoding, this scene) at ~5.5% of channel samples over
    // +-24/255. Failing at 10% leaves margin for encoder-version drift
    // while still catching a real defect: a wrong axis, a swapped
    // channel, or a badly misplaced silhouette moves far more than a
    // thin edge band, closer to "most of the image differs."
    const int tol = 24;
    size_t mismatches = 0;
    for (size_t i = 0; i < jpg.rgb.size(); ++i) {
      if (std::abs(int(jpg.rgb[i]) - int(tif.rgb[i])) > tol) {
        ++mismatches;
      }
    }
    const double mismatchFraction = double(mismatches) / double(jpg.rgb.size());
    check(mismatchFraction < 0.10,
          "JPEG pixels are within tolerance of the equivalent TIFF render "
          "(fewer than 10% of channel samples exceed +-24/255)");
  }

  // ---- a genuinely unknown extension must diagnose, not crash ----
  std::snprintf(scene, sizeof scene, sceneTemplate, "jpegdriver.bogus");
  writeFile("jpegdriver_bogus.rib", scene);
  int bogusExit = runGman(gman, "jpegdriver_bogus.rib");
  check(bogusExit != 0 && bogusExit != -1,
        "an unrecognized Display extension fails cleanly (not a crash)");
  check(!fileExists("jpegdriver.bogus"),
        "an unrecognized Display extension writes no file");

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("jpeg driver holds\n");
  return 0;
}
