/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Defect 2 (uat-defects prompt): the PNG output driver segfaulted (exit
 * 139) leaving a zero-byte file. Three bugs in GMANOutputPNG::save,
 * gmanoutputpng.cpp:
 *
 * 1. It called png_read_update_info on a png_ptr from
 *    png_create_write_struct. That function's own doc says it "MUST be
 *    called before png_read_update_info or png_start_read_image" -- a
 *    read-side call, on a struct never set up for reading. It corrupted
 *    internal libpng state that later crashed deep inside
 *    png_write_image's compression path. This was the segfault.
 * 2. The pixel buffer was sized rowbytes*xres instead of rowbytes*yres --
 *    invisible on a square Format, where the two are equal, which is why
 *    this survived as long as it did.
 * 3. The inner x-loop that packs each row ended in `break`, so only pixel
 *    x=0 of every row was ever written; every other pixel byte was
 *    whatever `new[]` happened to return.
 *
 * Proof: a PNG render must exit 0, decode as a well-formed PNG of the
 * requested size, and match an equivalent TIFF render of the same scene
 * pixel for pixel -- (3) alone would pass an exit-status or file-size
 * check while writing an image that is visibly wrong past its first
 * column.
 *
 * Revert check: reverting any of the three closes coverage differently --
 * (1) makes both "renders" checks go red (back to exit 139); (2) does not
 * reproduce on this test's square Format, which is why (3) matters too;
 * (3) alone (with (1) and (2) fixed) leaves "renders" green but every
 * pixel-match assertion red.
 */

#include <sys/wait.h>

#include <png.h>
#include <tiffio.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <sys/stat.h>

#include "check.h"

namespace {

// `output` is removed before the run. Every content assertion below would
// otherwise be satisfied by an image an earlier run left behind: a build
// directory is reused across ctest invocations, and a reverted fix that
// throws before opening the display leaves the previous good file in place.
// tests/baseline_test.cpp removes its target for the same reason.
int runGman(const std::string &gman, const std::string &rib,
            const std::string &output) {
  std::remove(output.c_str());
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

struct Image {
  bool ok = false;
  int width = 0, height = 0;
  std::vector<unsigned char> rgb; // 3 bytes/pixel, row-major, top to bottom
};

// Decodes a PNG to top-down 8-bit RGB, dropping alpha -- GMANOutputPNG
// always writes PNG_COLOR_TYPE_RGB_ALPHA, so this expects exactly that.
Image readPNG(const std::string &path) {
  Image img;
  FILE *fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    return img;
  }

  png_structp png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (png_ptr == nullptr) {
    std::fclose(fp);
    return img;
  }
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == nullptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    std::fclose(fp);
    return img;
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    std::fclose(fp);
    img = Image();
    return img;
  }

  png_init_io(png_ptr, fp);
  png_read_info(png_ptr, info_ptr);

  png_uint_32 width = 0, height = 0;
  int bitDepth = 0, colorType = 0;
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bitDepth, &colorType,
               nullptr, nullptr, nullptr);

  // Normalize to 8-bit RGBA regardless of what's on disk, so this reader
  // works whether or not the driver still emits exactly RGB_ALPHA.
  if (bitDepth == 16) png_set_strip_16(png_ptr);
  if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
  if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
  if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png_ptr);
  if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_PALETTE ||
      colorType == PNG_COLOR_TYPE_GRAY)
    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);
  png_read_update_info(png_ptr, info_ptr); // read side: legitimate here

  png_uint_32 rowbytes = png_get_rowbytes(png_ptr, info_ptr);
  std::vector<png_byte> buffer(rowbytes * height);
  std::vector<png_bytep> rows(height);
  for (png_uint_32 y = 0; y < height; ++y) {
    rows[y] = buffer.data() + y * rowbytes;
  }
  png_read_image(png_ptr, rows.data());
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  std::fclose(fp);

  img.ok = true;
  img.width = int(width);
  img.height = int(height);
  img.rgb.resize(size_t(width) * height * 3);
  for (png_uint_32 y = 0; y < height; ++y) {
    for (png_uint_32 x = 0; x < width; ++x) {
      png_bytep px = rows[y] + x * 4;
      size_t dst = (size_t(y) * width + x) * 3;
      img.rgb[dst + 0] = px[0];
      img.rgb[dst + 1] = px[1];
      img.rgb[dst + 2] = px[2];
    }
  }
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
  std::snprintf(scene, sizeof scene, sceneTemplate, "pngdriver.png");
  writeFile("pngdriver.rib", scene);
  check(runGman(gman, "pngdriver.rib", "pngdriver.png") == 0, "PNG scene renders");
  check(nonEmptyFile("pngdriver.png"), "PNG file is non-empty");

  std::snprintf(scene, sizeof scene, sceneTemplate, "pngdriver.tif");
  writeFile("pngdriver_tif.rib", scene);
  check(runGman(gman, "pngdriver_tif.rib", "pngdriver.tif") == 0, "equivalent TIFF scene renders");

  Image png = readPNG("pngdriver.png");
  check(png.ok, "PNG decodes as well-formed");
  check(png.width == 64 && png.height == 64, "PNG has the requested dimensions");

  Image tif = readTIFF("pngdriver.tif");
  check(tif.ok, "TIFF decodes as well-formed");

  if (png.ok && tif.ok && png.width == tif.width && png.height == tif.height) {
    bool allMatch = png.rgb == tif.rgb;
    check(allMatch, "PNG pixels match the equivalent TIFF render exactly");

    // A silhouette has to be present past the first column -- catches the
    // "only x=0 was written" shape directly, independent of the TIFF
    // comparison above.
    bool nonFirstColumnDiffers = false;
    const unsigned char bg0 = png.rgb[0], bg1 = png.rgb[1], bg2 = png.rgb[2];
    for (int y = 0; y < png.height && !nonFirstColumnDiffers; ++y) {
      for (int x = 1; x < png.width; ++x) {
        size_t i = (size_t(y) * png.width + x) * 3;
        if (png.rgb[i] != bg0 || png.rgb[i + 1] != bg1 || png.rgb[i + 2] != bg2) {
          nonFirstColumnDiffers = true;
          break;
        }
      }
    }
    check(nonFirstColumnDiffers,
          "PNG has non-background content past column 0");
  }

  return checkSummary("png driver holds");
}
