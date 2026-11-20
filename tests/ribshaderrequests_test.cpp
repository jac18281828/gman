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
 * A RIB Atmosphere, Displacement or Imager reaches the renderer as the C
 * API's call does. Naming a shader with no plugin reports RIE_NOSHADER at
 * RIE_ERROR, which stops the file: exit 1, no image, the report naming the
 * request, the shader and the request's fallback. Naming a loadable shader
 * with a one-entry parameter list renders the control scene's exact image,
 * since no renderer runs these shaders; a miscounted list would desync the
 * next request and change the image.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

struct ShaderRequest {
  std::string request;
  std::string fixtureStem;
  std::string missingShader;
  std::string loadableShader;
  std::string fallback;
};

std::vector<ShaderRequest> const requests = {
    {"Atmosphere", "ribshaderrequests_atmosphere", "nosuchatmosphere", "countingvolume",
     "no atmosphere shades the volume"},
    {"Displacement", "ribshaderrequests_displacement", "nosuchdisplacement", "identitydisplacement",
     "the surface renders undisplaced"},
    {"Imager", "ribshaderrequests_imager", "nosuchimager", "countingimager", "the frame renders without an imager"},
};

struct Rendered {
  GMANRunResult result;
  GmanImage image;
};

Rendered renderFixture(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                       std::string const& stem) {
  const std::string tifName = stem + ".tif";
  std::remove(tifName.c_str());
  Rendered rendered;
  rendered.result = runGman(gman, {"-r", renderer, ribDir + "/" + stem + ".rib"});
  rendered.image = readGmanTIFF(tifName);
  return rendered;
}

int countOccurrences(std::string const& haystack, std::string const& needle) {
  int count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

void checkMissingShader(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                        ShaderRequest const& request) {
  const std::string tag = request.request + " missing";
  const std::string stem = request.fixtureStem + "_missing";
  Rendered const rendered = renderFixture(gman, renderer, ribDir, stem);
  std::string const& output = rendered.result.output;

  check(rendered.result.exitStatus == 1, tag + ": " + stem + " exits 1");
  check(!rendered.image.ok, tag + ": writes no TIFF");
  check(countOccurrences(output, "ERROR: RIE_NOSHADER") == 1, tag + ": reports ERROR: RIE_NOSHADER exactly once");
  check(output.find(request.request + " \"" + request.missingShader + "\"") != std::string::npos,
        tag + ": names " + request.request + " \"" + request.missingShader + "\"");
  check(output.find("failed to load") != std::string::npos, tag + ": reports failed to load");
  check(output.find(request.fallback) != std::string::npos, tag + ": reports \"" + request.fallback + "\"");
  check(output.find("SEVERE:") == std::string::npos, tag + ": reports no SEVERE");
}

void checkLoadableShader(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                         ShaderRequest const& request, GmanImage const& control) {
  const std::string tag = request.request + " loadable";
  const std::string stem = request.fixtureStem + "_loaded";
  Rendered const rendered = renderFixture(gman, renderer, ribDir, stem);

  check(rendered.result.exitStatus == 0, tag + ": " + stem + " exits 0");
  check(rendered.image.ok, tag + ": writes its TIFF");
  check(rendered.result.output.find("ERROR:") == std::string::npos, tag + ": reports no error");
  check(rendered.image.ok && control.ok && rendered.image.width == control.width &&
            rendered.image.height == control.height && rendered.image.raster == control.raster,
        tag + ": renders the control scene's pixels");
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

  Rendered const control = renderFixture(gman, renderer, ribDir, "ribshaderrequests_control");
  check(control.result.exitStatus == 0, "control: ribshaderrequests_control exits 0");
  check(control.image.ok, "control: writes its TIFF");

  for (ShaderRequest const& request : requests) {
    checkMissingShader(gman, renderer, ribDir, request);
    checkLoadableShader(gman, renderer, ribDir, request, control.image);
  }

  return checkSummary("a RIB Atmosphere, Displacement and Imager reach the renderer");
}
