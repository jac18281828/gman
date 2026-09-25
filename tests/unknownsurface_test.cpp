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
 * A Surface naming a module that will not load -- missing entirely, opening
 * but exporting no shader, or exporting a shader that is not a surface --
 * reports RIE_NOSHADER and renders on with the default surface, matte,
 * instead of losing the frame. Three fixtures each reach one of those three
 * failures; their renders must match a control scene that never names the
 * bad Surface, pixel for pixel, since the failed request's parameters must
 * not reach the default shader either.
 */

#include <cstdio>
#include <string>

#include <sys/wait.h>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"

namespace {

struct Result {
  int exitStatus;
  std::string output;
};

Result runGman(std::string const& gman, std::string const& renderer, std::string const& rib) {
  const std::string command = "\"" + gman + "\" -r " + renderer + " \"" + rib + "\" 2>&1";
  std::FILE* pipe = popen(command.c_str(), "r");
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

// Runs one fixture, removing its own TIFF first so a stale image from an
// earlier run cannot pass the render check. Returns the render result and
// the freshly read TIFF, or an unread GmanImage if the render failed.
struct Rendered {
  Result result;
  GmanImage image;
};

Rendered renderFixture(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                       std::string const& ribName, std::string const& tifName) {
  std::remove(tifName.c_str());
  Rendered rendered;
  rendered.result = runGman(gman, renderer, ribDir + "/" + ribName);
  rendered.image = readGmanTIFF(tifName);
  return rendered;
}

// Every pixel's R, G, B and A match the control exactly.
void checkPixelIdentical(GmanImage const& actual, GmanImage const& control, std::string const& tag) {
  if (!actual.ok || !control.ok) {
    check(false, tag + ": both images read back before comparing to the control");
    return;
  }
  check(actual.width == control.width && actual.height == control.height, tag + ": dimensions match the control");
  if (actual.width != control.width || actual.height != control.height) {
    return;
  }
  long mismatched = 0;
  for (uint32_t y = 0; y < actual.height; ++y) {
    for (uint32_t x = 0; x < actual.width; ++x) {
      const uint32_t a = actual.at(x, y);
      const uint32_t c = control.at(x, y);
      if (TIFFGetR(a) != TIFFGetR(c) || TIFFGetG(a) != TIFFGetG(c) || TIFFGetB(a) != TIFFGetB(c) ||
          TIFFGetA(a) != TIFFGetA(c)) {
        ++mismatched;
      }
    }
  }
  check(mismatched == 0, tag + ": every pixel matches the control exactly, all four channels (" +
                             std::to_string(mismatched) + " differed)");
}

// The report names RIE_NOSHADER and the failing Surface name, and carries
// no SEVERE report -- the frame renders on, it does not abort.
void checkReport(std::string const& output, std::string const& failingName, std::string const& tag) {
  check(output.find("ERROR: RIE_NOSHADER") != std::string::npos, tag + ": reports ERROR: RIE_NOSHADER");
  check(output.find("Surface \"" + failingName + "\"") != std::string::npos,
        tag + ": names Surface \"" + failingName + "\"");
  check(output.find("SEVERE:") == std::string::npos, tag + ": reports no SEVERE");
}

void checkFallback(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                   std::string const& ribName, std::string const& tifName, std::string const& failingName,
                   GmanImage const& control, std::string const& tag) {
  Rendered rendered = renderFixture(gman, renderer, ribDir, ribName, tifName);

  // Exits 0 and writes its TIFF.
  check(rendered.result.exitStatus == 0, tag + ": " + ribName + " exits 0");
  check(rendered.image.ok, tag + ": " + ribName + " writes its TIFF");

  checkReport(rendered.result.output, failingName, tag);
  checkPixelIdentical(rendered.image, control, tag);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib-dir> <renderer>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];
  const std::string ribDir = argv[2];
  const std::string renderer = argv[3];

  // Control: the same scene, no Surface call, so its render is the default
  // surface's own, untainted baseline.
  Rendered control = renderFixture(gman, renderer, ribDir, "unknownsurface_control.rib", "unknownsurface_control.tif");
  check(control.result.exitStatus == 0, "control: unknownsurface_control.rib exits 0");
  check(control.image.ok, "control: unknownsurface_control.rib writes its TIFF");

  // The control is a real render, not an accidentally blank frame.
  if (control.image.ok) {
    const uint32_t corner = control.image.at(0, 0);
    check(TIFFGetA(corner) == 0, "control: corner pixel alpha is 0");

    const uint32_t centre = control.image.at(control.image.width / 2, control.image.height / 2);
    check(TIFFGetA(centre) > 0, "control: centre pixel alpha is above 0");
    check(TIFFGetR(centre) > 0 || TIFFGetG(centre) > 0 || TIFFGetB(centre) > 0,
          "control: centre pixel has a colour channel above 0");
  }

  checkFallback(gman, renderer, ribDir, "unknownsurface.rib", "unknownsurface.tif", "nosuchshader", control.image,
                "no module");
  checkFallback(gman, renderer, ribDir, "unknownsurface_noshader.rib", "unknownsurface_noshader.tif", "gmanzbuffer",
                control.image, "no GMANLoadShader");
  checkFallback(gman, renderer, ribDir, "unknownsurface_volume.rib", "unknownsurface_volume.tif", "notasurface",
                control.image, "not a surface");

  return checkSummary("an unknown Surface reports RIE_NOSHADER and falls back to the default surface");
}
