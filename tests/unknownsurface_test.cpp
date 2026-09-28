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
 * but exporting no shader, exporting a shader that is not a surface, or
 * missing GMANDestroyShader -- reports RIE_NOSHADER at RIE_ERROR. gman's
 * error handler prints that report and stops the file: exit 1, no image.
 * Four fixtures each reach one of those four failures and name their own
 * failing Surface in the report; a control scene that never names a bad
 * Surface proves the renderer itself still works.
 */

#include <cstdio>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

GMANRunResult runGman(std::string const& gman, std::string const& renderer, std::string const& rib) {
  return ::runGman(gman, {"-r", renderer, rib});
}

// Runs one fixture, removing its own TIFF first so a stale image from an
// earlier run cannot pass the render check. Returns the render result and
// the freshly read TIFF, or an unread GmanImage if the render failed.
struct Rendered {
  GMANRunResult result;
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

// Counts non-overlapping occurrences of needle in haystack.
int countOccurrences(std::string const& haystack, std::string const& needle) {
  int count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

// The report names RIE_NOSHADER exactly once and the failing Surface
// name, and carries no SEVERE report -- the stop is the handler's, not a
// crash.
void checkReport(std::string const& output, std::string const& failingName, std::string const& tag) {
  check(countOccurrences(output, "ERROR: RIE_NOSHADER") == 1, tag + ": reports ERROR: RIE_NOSHADER exactly once");
  check(output.find("Surface \"" + failingName + "\"") != std::string::npos,
        tag + ": names Surface \"" + failingName + "\"");
  check(output.find("SEVERE:") == std::string::npos, tag + ": reports no SEVERE");
}

Rendered checkStopped(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                      std::string const& ribName, std::string const& tifName, std::string const& failingName,
                      std::string const& tag) {
  Rendered rendered = renderFixture(gman, renderer, ribDir, ribName, tifName);

  // The report stops the file: exit 1, no image.
  check(rendered.result.exitStatus == 1, tag + ": " + ribName + " exits 1");
  check(!rendered.image.ok, tag + ": " + ribName + " writes no TIFF");

  checkReport(rendered.result.output, failingName, tag);
  return rendered;
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

  // Control: the same scene, no Surface call, proving the renderer draws
  // the scene under the default surface.
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

  checkStopped(gman, renderer, ribDir, "unknownsurface.rib", "unknownsurface.tif", "nosuchshader", "no module");
  checkStopped(gman, renderer, ribDir, "unknownsurface_noshader.rib", "unknownsurface_noshader.tif", "gmanzbuffer",
               "no GMANLoadShader");
  checkStopped(gman, renderer, ribDir, "unknownsurface_volume.rib", "unknownsurface_volume.tif", "notasurface",
               "not a surface");

  Rendered const nodestroy = checkStopped(gman, renderer, ribDir, "unknownsurface_nodestroy.rib",
                                          "unknownsurface_nodestroy.tif", "nodestroyshader", "no GMANDestroyShader");
  check(nodestroy.result.output.find("GMANDestroyShader") != std::string::npos,
        "no GMANDestroyShader: names GMANDestroyShader");

  return checkSummary("an unknown Surface reports RIE_NOSHADER, which stops the file");
}
