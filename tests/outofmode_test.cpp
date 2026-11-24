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
 * A request issued outside the blocks it is legal in reports RIE_ILLSTATE at
 * RIE_WARNING through the error handler, changes no state and is ignored, so
 * the file renders on: an Imager or an Exposure after WorldBegin writes the
 * control scene's exact image under one warning. A block request out of
 * place, an AttributeBegin inside ObjectBegin, still stops the file: exit 1,
 * no image. Through GMANRenderManImpl directly, an Imager after RiWorldBegin
 * throws nothing, reaches the handler once with the same report and leaves
 * the options without an imager.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "gmanrendermanimpl.h"
#include "goldenimage.h"
#include "ri.h"
#include "rungman.h"

namespace {

std::string const warningLine = "WARNING: RIE_ILLSTATE";
std::string const ignoredMessage = "Invalid mode for procedure; the request is ignored.";

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

void checkRendersOn(std::string const& gman, std::string const& renderer, std::string const& ribDir,
                    std::string const& stem, GmanImage const& control) {
  Rendered const rendered = renderFixture(gman, renderer, ribDir, stem);
  std::string const& output = rendered.result.output;

  check(rendered.result.exitStatus == 0, stem + ": exits 0");
  check(rendered.image.ok, stem + ": writes its TIFF");
  check(countOccurrences(output, warningLine) == 1, stem + ": reports WARNING: RIE_ILLSTATE exactly once");
  check(output.find(ignoredMessage) != std::string::npos, stem + ": says the request is ignored");
  check(output.find("ERROR:") == std::string::npos, stem + ": reports no ERROR");
  check(output.find("SEVERE:") == std::string::npos, stem + ": reports no SEVERE");
  check(rendered.image.ok && control.ok && rendered.image.width == control.width &&
            rendered.image.height == control.height && rendered.image.raster == control.raster,
        stem + ": renders the control scene's pixels");
}

void checkBlockStops(std::string const& gman, std::string const& renderer, std::string const& ribDir) {
  Rendered const rendered = renderFixture(gman, renderer, ribDir, "outofmode_block");
  std::string const& output = rendered.result.output;

  check(rendered.result.exitStatus == 1, "outofmode_block: exits 1");
  check(!rendered.image.ok, "outofmode_block: writes no TIFF");
  check(countOccurrences(output, warningLine) == 1, "outofmode_block: reports WARNING: RIE_ILLSTATE exactly once");
  check(output.find("Invalid mode for procedure") != std::string::npos, "outofmode_block: names the invalid mode");
  check(output.find(ignoredMessage) == std::string::npos, "outofmode_block: does not call the request ignored");
}

struct RecordedError {
  RtInt code;
  RtInt severity;
  std::string message;
};

std::vector<RecordedError> recorded;

RtVoid recordingErrorHandler(RtInt code, RtInt severity, char const* message) {
  recorded.push_back({code, severity, message});
}

class OptionReadingRenderMan : public GMANRenderManImpl {
public:
  GMANOptions& options() { return getOptions(); }
};

void checkRenderManIgnoresImager() {
  OptionReadingRenderMan impl;
  impl.RiWorldBegin();

  recorded.clear();
  impl.RiErrorHandler(recordingErrorHandler);

  RtColor background = {1.0f, 1.0f, 1.0f};
  RtToken tokens[1] = {"color background"};
  RtPointer parms[1] = {background};
  bool threw = false;
  try {
    impl.RiImagerV("background", 1, tokens, parms);
  } catch (...) {
    threw = true;
  }
  impl.RiErrorHandler(RiErrorPrint);

  check(!threw, "render manager: an Imager after RiWorldBegin throws nothing");
  check(recorded.size() == 1, "render manager: the handler sees exactly one report");
  check(!recorded.empty() && recorded.front().code == RIE_ILLSTATE && recorded.front().severity == RIE_WARNING,
        "render manager: the report is RIE_ILLSTATE at RIE_WARNING");
  check(!recorded.empty() && recorded.front().message == ignoredMessage,
        "render manager: the report says the request is ignored");
  check(impl.options().getImager() == nullptr, "render manager: the ignored Imager leaves no imager");
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

  Rendered const control = renderFixture(gman, renderer, ribDir, "outofmode_control");
  check(control.result.exitStatus == 0, "control: outofmode_control exits 0");
  check(control.image.ok, "control: writes its TIFF");
  check(control.result.output.find(warningLine) == std::string::npos, "control: reports no WARNING: RIE_ILLSTATE");

  checkRendersOn(gman, renderer, ribDir, "outofmode_imager", control.image);
  checkRendersOn(gman, renderer, ribDir, "outofmode_option", control.image);
  checkBlockStops(gman, renderer, ribDir);
  checkRenderManIgnoresImager();

  return checkSummary("a request out of mode is reported and ignored, and a block request out of place stops the file");
}
