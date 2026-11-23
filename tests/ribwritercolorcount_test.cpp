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

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "gmanascii.h"
#include "ri.h"

namespace {

constexpr char const* outputName = "ribwritercolorcount.rib";

struct ColorRequest {
  std::string name;
  std::vector<RtFloat> values;
  std::string label;
};

struct WrittenFile {
  std::vector<ColorRequest> colorRequests;
  int colorSamplesLines = 0;
};

class Script {
public:
  explicit Script(GMANASCII& writer) : writer(writer) {}

  void colorSamples(RtInt count) {
    std::vector<RtFloat> nRGB(3 * static_cast<std::size_t>(count), 0.0f);
    std::vector<RtFloat> RGBn(3 * static_cast<std::size_t>(count), 0.0f);
    writer.RiColorSamples(count, nRGB.data(), RGBn.data());
  }

  void color(RtInt count, std::string const& label) {
    std::vector<RtFloat> values = nextValues(count);
    writer.RiColor(values.data());
    expected.push_back({"Color", values, label});
  }

  void opacity(RtInt count, std::string const& label) {
    std::vector<RtFloat> values = nextValues(count);
    writer.RiOpacity(values.data());
    expected.push_back({"Opacity", values, label});
  }

  std::vector<ColorRequest> const& requests() const { return expected; }

private:
  std::vector<RtFloat> nextValues(RtInt count) {
    std::vector<RtFloat> values(static_cast<std::size_t>(count));
    for (RtFloat& value : values) {
      value = 0.125f * static_cast<RtFloat>(++issued);
    }
    return values;
  }

  GMANASCII& writer;
  std::vector<ColorRequest> expected;
  int issued = 0;
};

std::vector<RtFloat> bracketedFloats(std::string const& line) {
  const std::size_t open = line.find('[');
  const std::size_t close = line.find(']');
  if (open == std::string::npos || close == std::string::npos || close < open) {
    return {};
  }
  std::istringstream stream(line.substr(open + 1, close - open - 1));
  std::vector<RtFloat> values;
  for (RtFloat value = 0.0f; stream >> value;) {
    values.push_back(value);
  }
  return values;
}

WrittenFile readWrittenFile(std::string const& path) {
  WrittenFile file;
  std::ifstream in(path);
  for (std::string line; std::getline(in, line);) {
    const std::string word = line.substr(0, line.find(' '));
    if (word == "ColorSamples") {
      ++file.colorSamplesLines;
    } else if (word == "Color" || word == "Opacity") {
      file.colorRequests.push_back({word, bracketedFloats(line), ""});
    }
  }
  return file;
}

void checkWritten(WrittenFile const& file, std::vector<ColorRequest> const& expected) {
  check(file.colorRequests.size() == expected.size(), "the file holds every Color and Opacity request");
  if (file.colorRequests.size() != expected.size()) {
    return;
  }
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const ColorRequest& written = file.colorRequests[i];
    check(written.name == expected[i].name && written.values == expected[i].values, expected[i].label);
  }
}

} // namespace

int main() {
  GMANASCII writer;
  Script script(writer);

  writer.RiBegin(outputName);

  script.colorSamples(1);
  script.color(1, "one sample: Color carries one value");
  script.opacity(1, "one sample: Opacity carries one value");

  script.colorSamples(5);
  script.color(5, "five samples: Color carries five values");
  script.opacity(5, "five samples: Opacity carries five values");

  writer.RiFrameEnd();
  script.color(5, "a FrameEnd with no open frame leaves the count");

  writer.RiFrameBegin(1);
  script.colorSamples(1);
  script.color(1, "inside a frame: Color carries the frame's count");
  writer.RiFrameEnd();
  script.color(5, "after FrameEnd: Color carries the restored count");

  writer.RiFrameEnd();
  script.color(5, "a FrameEnd after the frame leaves the restored count");

  writer.RiEnd();

  checkWritten(readWrittenFile(outputName), script.requests());

  return checkSummary("GMANASCII writes Color and Opacity at the ColorSamples count");
}
