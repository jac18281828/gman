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
 * `Surface` before `AttributeBegin`/`FrameBegin` must not double-free.
 * Both requests push
 * GMANGraphicState::attributesStack.push(attributesStack.top()), which
 * copies GMANAttributes -- correct RenderMan semantics, since an
 * attribute block inherits its parent's attributes. GMANAttributes'
 * six shader-module members are std::shared_ptr<GMANLoadableShader>: a
 * raw GMANLoadableShader* with no copy protection would shallow-copy on
 * push, so the pushed copy and the original would hold the same
 * pointer and double-free it at their respective destructors. A loaded
 * module is immutable once loaded, so sharing it rather than
 * deep-copying also avoids re-dlopen'ing on every
 * AttributeBegin/AttributeEnd pair that does not change shaders.
 *
 * Not covered here: SolidBegin shares the same attributesStack shape but
 * cannot be reached from RIB (GMANRIBParse::parseSolidBegin never calls
 * RiSolidBeginV) -- a .rib fixture through it would pass on broken code.
 *
 * Revert check: reverting GMANAttributes back to raw GMANLoadableShader*
 * members (no explicit copy protection) reintroduces the double free --
 * both scenes below go back to a non-zero/signal exit status.
 *
 * The pixel checks prove the copy carries its state: the block and the
 * frame each inherit the surface and its Kd, which the default matte
 * would not reproduce. Each scene declares its light after the state
 * under test, so a copy that loses only the surface reads differently
 * from one that loses only the light. Under a unit distant light from
 * behind the camera, Surface "matte" "Kd" [0.1] gives the sphere's
 * centre a red of about 25; the default matte gives 254 and an unlit
 * frame gives 0.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <tiffio.h>

#include "check.h"
#include "goldenimage.h"
#include "rungman.h"

namespace {

// The centre red of a sphere shaded by Kd 0.1 under a unit light, about 25 of
// 255. The default matte (254) and an unlit frame (0) both fall outside.
constexpr uint32_t centreRedMin = 12;
constexpr uint32_t centreRedMax = 50;

int runGman(const std::string& gman, const std::string& rib) { return ::runGman(gman, {rib}).exitStatus; }

void writeFile(const std::string& path, const std::string& contents) {
  std::ofstream out(path);
  out << contents;
}

// Checks the corner is background and the centre is the sphere shaded by the
// inherited surface.
void checkInheritedSurface(std::string const& image, std::string const& scene) {
  GmanImage const img = readGmanTIFF(image);
  check(img.ok, scene + ": TIFF reads back");
  if (!img.ok) {
    return;
  }
  check(TIFFGetA(img.at(0, 0)) == 0, scene + ": corner pixel is background");
  uint32_t const centre = img.at(img.width / 2, img.height / 2);
  uint32_t const red = TIFFGetR(centre);
  check(TIFFGetA(centre) > 0, scene + ": centre pixel is covered");
  check(red >= centreRedMin && red <= centreRedMax,
        scene + ": centre red " + std::to_string(red) + " lies in the window the inherited Kd 0.1 gives (" +
            std::to_string(centreRedMin) + " to " + std::to_string(centreRedMax) + ")");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <gman-binary>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];

  // ---- Surface declared once, then used inside AttributeBegin/End. The
  // light follows the state under test, inside the block ----
  const char* attrRib = "Display \"attr.tif\" \"file\" \"rgba\"\n"
                        "Format 64 64 1\n"
                        "Projection \"perspective\" \"fov\" [45]\n"
                        "Clipping 0.5 50\n"
                        "Translate 0 0 5\n"
                        "WorldBegin\n"
                        "Surface \"matte\" \"Kd\" [0.1]\n"
                        "AttributeBegin\n"
                        "  LightSource \"distantlight\" 1 \"intensity\" [1.0] \"from\" [0 0 -5] \"to\" [0 0 0]\n"
                        "  Sphere 1 -1 1 360\n"
                        "AttributeEnd\n"
                        "WorldEnd\n";
  writeFile("attr.rib", attrRib);
  // Removed ahead of the run: an earlier run's image left in a reused
  // build directory would otherwise satisfy the content assertion below
  // even if this run throws before opening the display.
  std::remove("attr.tif");
  check(runGman(gman, "attr.rib") == 0, "Surface before AttributeBegin runs to completion");
  checkInheritedSurface("attr.tif", "AttributeBegin scene");

  // ---- Surface declared once, then a FrameBegin/FrameEnd pair (outside
  // WorldBegin, with a frame number -- nested inside WorldBegin fails on an
  // unrelated RIE_ILLSTATE and would be a false negative here). The light
  // follows WorldBegin ----
  const char* frameRib = "Display \"frame.tif\" \"file\" \"rgba\"\n"
                         "Format 64 64 1\n"
                         "Projection \"perspective\" \"fov\" [45]\n"
                         "Clipping 0.5 50\n"
                         "Translate 0 0 5\n"
                         "Surface \"matte\" \"Kd\" [0.1]\n"
                         "FrameBegin 1\n"
                         "WorldBegin\n"
                         "LightSource \"distantlight\" 1 \"intensity\" [1.0] \"from\" [0 0 -5] \"to\" [0 0 0]\n"
                         "Sphere 1 -1 1 360\n"
                         "WorldEnd\n"
                         "FrameEnd\n";
  writeFile("frame.rib", frameRib);
  std::remove("frame.tif");
  check(runGman(gman, "frame.rib") == 0, "Surface before FrameBegin runs to completion");
  checkInheritedSurface("frame.tif", "FrameBegin scene");

  return checkSummary("attributes copy holds");
}
