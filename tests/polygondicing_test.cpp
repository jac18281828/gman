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
 * Guards buildPolygonObject's own dicing resolution (kPolygonDiceN,
 * gmanpatchpolyobjectmanager.cpp) against a future, silent coarsening: one
 * textured square, rendered two ways over the identical geometry and
 * texture -- once as a single Polygon (ordinary dicing), once as a grid of
 * kGroundTruthGrid^2 small Polygon requests tiling the same square (a much
 * finer hand-diced equivalent). Every vertex, in either fixture, defaults
 * its own s, t to its own object x, y, so both fixtures shade the
 * identical continuous texture-mapped surface; only how many vertices
 * sample it before Gouraud interpolation takes over differs. At the
 * production dicing resolution the two agree within checkGoldenImage's
 * own tolerance; forced coarser, they do not.
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "check.h"
#include "checkertexture.h"
#include "goldenimage.h"

namespace {

// Nine times finer per edge than buildPolygonObject's own dicing cap
// (kPolygonDiceN, 16 divisions per edge) -- every triangle in both
// fixtures projects well past 15 raster pixels on its longest edge (this
// file's own commonHeader comment), so both still hit that cap
// (gmanpatchpolyobjectmanager.cpp's diceCountFor), and each of this grid's
// own small quads is diced again at the same 16 once it reaches the
// shared tail. The ground truth then samples the texture roughly 8*16
// times across the square's own width, comfortably past where further
// refinement would move a rendered pixel.
const int kGroundTruthGrid = 8;

int runGman(const std::string& gman, const std::string& rib) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void writeFile(const std::string& path, const std::string& contents) {
  std::ofstream out(path);
  out << contents;
}

// Every line both fixtures share: a 200x200 perspective camera looking at
// world z=0 down +z (AGENTS.md's RIB authoring convention), an ambient-only
// light at 0.3 and a paintedplastic surface reading the checker texture at
// Ka=1, Kd=0, Ks=0 -- texcoords_test.cpp's own frame. World x, y in
// [-4,4] fills raster [20,180] of the 200x200 frame (texture.rib's own
// raster = 20*x+100), most of the image, for a large enough sample to make
// a coarse-dicing regression bite.
std::string commonHeader(const std::string& displayName) {
  return "Display \"" + displayName +
         ".tif\" \"file\" \"rgba\"\n"
         "Format 200 200 1\n"
         "Projection \"perspective\" \"fov\" [90]\n"
         "Clipping 0.5 50\n"
         "Translate 0 0 5\n"
         "WorldBegin\n"
         "LightSource \"ambientlight\" 1 \"intensity\" [0.3]\n"
         "Sides 2\n"
         "Color [1 1 1]\n"
         "Surface \"paintedplastic\" \"Ka\" [1.0] \"Kd\" [0.0] \"Ks\" [0.0]\n"
         "\t\"texturename\" [\"checker_texture.tif\"]\n"
         "Translate -4 4 0\n"
         "Scale 8 -8 1\n";
}

// One Polygon over the whole unit square -- ordinary dicing, the fix under
// test.
std::string dicedSquareRib() {
  std::string rib = commonHeader("polygondicing_diced");
  rib += "Polygon \"P\" [0 0 0  1 0 0  1 1 0  0 1 0]\n";
  rib += "WorldEnd\n";
  return rib;
}

// A kGroundTruthGrid x kGroundTruthGrid grid of small Polygon requests
// tiling the identical unit square. Each cell's own four corners carry
// their own true object (x, y) -- the same default-s,t rule the single
// square above resolves at only four points -- so this fixture samples the
// same continuous texture-mapped surface at many more points before
// Gouraud interpolation ever runs.
std::string groundTruthSquareRib() {
  std::string rib = commonHeader("polygondicing_groundtruth");
  char cell[256];
  for (int j = 0; j < kGroundTruthGrid; ++j) {
    for (int i = 0; i < kGroundTruthGrid; ++i) {
      const double x0 = (double)i / kGroundTruthGrid;
      const double x1 = (double)(i + 1) / kGroundTruthGrid;
      const double y0 = (double)j / kGroundTruthGrid;
      const double y1 = (double)(j + 1) / kGroundTruthGrid;
      std::snprintf(cell, sizeof(cell), "Polygon \"P\" [%g %g 0  %g %g 0  %g %g 0  %g %g 0]\n", x0, y0, x1, y0, x1, y1,
                    x0, y1);
      rib += cell;
    }
  }
  rib += "WorldEnd\n";
  return rib;
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <gman-binary>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];

  check(writeCheckerTexture("checker_texture.tif"), "checker_texture.tif writes into the render's working directory");

  writeFile("polygondicing_diced.rib", dicedSquareRib());
  writeFile("polygondicing_groundtruth.rib", groundTruthSquareRib());

  check(runGman(gman, "polygondicing_diced.rib") == 0, "polygondicing_diced.rib renders");
  check(runGman(gman, "polygondicing_groundtruth.rib") == 0, "polygondicing_groundtruth.rib renders");

  checkGoldenImage("polygondicing_diced.tif", "polygondicing_groundtruth.tif", GOLDEN_CHANNEL_TOL, GOLDEN_MAX_FRACTION,
                   "polygondicing_diff.tif");

  return checkSummary("polygondicing holds");
}
