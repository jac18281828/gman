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
 * RIB's PixelVariance reaches GMANOptions: a fixture carrying one parses
 * in process, the options read its value, the Clipping after it still
 * parses and the log never reports an unrecognized request. A freshly
 * constructed GMANOptions reads 0, whatever its storage held before.
 */

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "gmanlog.h"
#include "gmanoptions.h"
#include "gmanrendermanimpl.h"
#include "gmanribparse.h"
#include "ri.h"

namespace {

constexpr RtFloat kFixtureVariance = 0.05f;
constexpr RtFloat kFixtureHither = 0.5f;
constexpr RtFloat kFixtureYon = 50.0f;
constexpr unsigned char kFill = 0xFF;
char const* const kParseLog = "ribpixelvariance_parse.log";

class VarianceReadingRenderMan : public GMANRenderManImpl {
public:
  std::vector<std::pair<RtFloat, RtFloat>> clippings;

  GMANOptions& options() { return getOptions(); }

  RtVoid RiClipping(RtFloat hither, RtFloat yon) override {
    clippings.emplace_back(hither, yon);
    GMANRenderManImpl::RiClipping(hither, yon);
  }
};

std::string readFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

void beginLogCapture(std::string const& path) {
  std::remove(path.c_str());
  setLogFile(path.c_str());
  setScreenOutput(false);
}

std::string endLogCapture(std::string const& path) {
  setLogFile("/dev/null");
  setScreenOutput(true);
  return readFile(path);
}

void testFixtureReachesOptions(std::string const& ribDir) {
  std::string const rib = ribDir + "/pixelvariance.rib";
  VarianceReadingRenderMan renderMan;
  renderMan.RiBegin("gmanzbuffer");

  beginLogCapture(kParseLog);
  {
    GMANRIBParse parser(renderMan, rib.c_str());
    parser.parse();
  }
  std::string const log = endLogCapture(kParseLog);

  check(renderMan.options().getPixelVariance() == kFixtureVariance, "PixelVariance 0.05 reaches the options");
  bool const clippingIntact = renderMan.clippings.size() == 1 && renderMan.clippings[0].first == kFixtureHither &&
                              renderMan.clippings[0].second == kFixtureYon;
  check(clippingIntact, "the Clipping after PixelVariance still parses");
  check(log.find("skipping unrecognized request") == std::string::npos,
        "the log holds no skipped request for PixelVariance");

  renderMan.RiEnd();
}

void testInitializerZeroesPixelVariance() {
  alignas(GMANOptions) unsigned char storage[sizeof(GMANOptions)];
  std::memset(storage, kFill, sizeof(storage));

  auto* const options = new (storage) GMANOptions();
  check(options->getPixelVariance() == 0.0f, "a constructed GMANOptions reads a PixelVariance of 0");
  options->~GMANOptions();
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <tests/rib dir>\n", argv[0]);
    return 2;
  }

  testFixtureReachesOptions(argv[1]);
  testInitializerZeroesPixelVariance();

  return checkSummary("RIB PixelVariance reaches GMANOptions, which reads 0 when unset");
}
