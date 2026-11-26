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
 * The image check of the install test (tests/install_test.cmake), which
 * runs it on every image an installed gman or an installed consumer writes.
 *
 *   gman_installimage <image.tif> <width> <height> [lit]
 *
 * Exits 0 when the TIFF opens and its width and height equal the arguments
 * and, with `lit`, at least one pixel has a nonzero red, green or blue
 * channel. A mismatch prints one line to stderr naming the file, the size
 * expected and the size found, and exits 1. A malformed command line exits 2.
 */

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <tiffio.h>

#include "goldenimage.h"

namespace {

constexpr int kMismatchStatus = 1;
constexpr int kUsageStatus = 2;
constexpr int kArgumentsWithoutLit = 4;
constexpr int kArgumentsWithLit = 5;
constexpr int kImageArgument = 1;
constexpr int kWidthArgument = 2;
constexpr int kHeightArgument = 3;
constexpr int kLitArgument = 4;
constexpr std::string_view kLitWord = "lit";

int usage(char const* program) {
  std::fputs(std::format("usage: {} <image.tif> <width> <height> [lit]\n", program).c_str(), stderr);
  return kUsageStatus;
}

std::optional<std::uint32_t> parseDimension(std::string_view text) {
  std::uint32_t value = 0;
  auto const* const end = text.data() + text.size();
  const auto parsed = std::from_chars(text.data(), end, value);
  if (parsed.ec != std::errc() || parsed.ptr != end) {
    return std::nullopt;
  }
  return value;
}

bool hasLitPixel(GmanImage const& image) {
  for (const auto pixel : image.raster) {
    if (TIFFGetR(pixel) != 0 || TIFFGetG(pixel) != 0 || TIFFGetB(pixel) != 0) {
      return true;
    }
  }
  return false;
}

int reportSize(std::string const& path, std::uint32_t width, std::uint32_t height, std::string const& found) {
  std::fputs(std::format("{}: expected {}x{}, found {}\n", path, width, height, found).c_str(), stderr);
  return kMismatchStatus;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc != kArgumentsWithoutLit && argc != kArgumentsWithLit) {
    return usage(argv[0]);
  }
  const auto requireLit = argc == kArgumentsWithLit;
  if (requireLit && std::string_view(argv[kLitArgument]) != kLitWord) {
    return usage(argv[0]);
  }
  const auto width = parseDimension(argv[kWidthArgument]);
  const auto height = parseDimension(argv[kHeightArgument]);
  if (!width || !height) {
    return usage(argv[0]);
  }

  const auto path = std::string(argv[kImageArgument]);
  const auto image = readGmanTIFF(path);
  if (!image.ok) {
    return reportSize(path, *width, *height, "no readable TIFF");
  }
  if (image.width != *width || image.height != *height) {
    return reportSize(path, *width, *height, std::format("{}x{}", image.width, image.height));
  }
  if (requireLit && !hasLitPixel(image)) {
    std::fputs(std::format("{}: no pixel has a nonzero red, green or blue channel\n", path).c_str(), stderr);
    return kMismatchStatus;
  }
  return 0;
}
