/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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
 * A C API RiBegin whose context fails to start, because the renderer does not
 * load or the .rib file does not open, reports the error with no context
 * current and frees the context. Every later request, RiEnd included, raises
 * RIE_NOTSTARTED at RIE_SEVERE, "No active context", once per call until
 * RiBegin or RiContext selects a context. The context current before the
 * failed RiBegin stays open, and RiContext returns to it.
 *
 * The current context is process-wide, so this test is its own executable.
 */

#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "ri.h"

namespace {

struct Report {
  RtInt code;
  RtInt severity;
  std::string message;
  RtContextHandle current;
};

std::vector<Report> recorded;

RtVoid recordingErrorHandler(RtInt code, RtInt severity, char const* message) {
  recorded.push_back({code, severity, message, RiGetContext()});
}

template <typename Request> std::vector<Report> reportsFrom(Request request) {
  recorded.clear();
  request();
  return recorded;
}

void checkNoActiveContext(std::vector<Report> const& reports, std::string const& when) {
  check(reports.size() == 1, when + " reports exactly once");
  if (reports.empty()) {
    return;
  }
  check(reports[0].code == RIE_NOTSTARTED, when + " raises RIE_NOTSTARTED");
  check(reports[0].severity == RIE_SEVERE, when + " raises at RIE_SEVERE");
  check(reports[0].message == "No active context",
        when + " says exactly \"No active context\", never \"No renderer loaded.\"");
}

void checkFailedBegin(std::vector<Report> const& reports, RtInt code, std::string const& when) {
  check(reports.size() == 1, when + " reports exactly once");
  if (reports.empty()) {
    return;
  }
  check(reports[0].code == code, when + " reports the load error");
  check(reports[0].current == RI_NULL, when + " reports with no context current");
}

void checkFailedRendererLoad() {
  RiBegin(RI_NULL);
  RtContextHandle const outer = RiGetContext();
  check(outer != RI_NULL, "RiBegin(RI_NULL) makes a context current");

  checkFailedBegin(reportsFrom([] { RiBegin("gmannosuchrenderer"); }), RIE_SYSTEM, "a renderer that fails to load");
  check(RiGetContext() == RI_NULL, "the failed RiBegin leaves no context current");

  checkNoActiveContext(reportsFrom([] { RiFormat(8, 8, 1); }), "RiFormat");
  checkNoActiveContext(reportsFrom([] { RiWorldBegin(); }), "RiWorldBegin");
  for (int i = 1; i <= 3; ++i) {
    checkNoActiveContext(reportsFrom([] { RiSphere(1, -1, 1, 360, RI_NULL); }), "RiSphere " + std::to_string(i));
  }
  RtPoint triangle[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  checkNoActiveContext(reportsFrom([&triangle] { RiPolygon(3, RI_P, triangle, RI_NULL); }), "RiPolygon");
  checkNoActiveContext(reportsFrom([] { RiWorldEnd(); }), "RiWorldEnd");
  checkNoActiveContext(reportsFrom([] { RiEnd(); }), "RiEnd");

  check(reportsFrom([outer] { RiContext(outer); }).empty(), "RiContext(outer) reports nothing");
  check(RiGetContext() == outer, "RiContext returns to the context open before the failed RiBegin");
  check(reportsFrom([] {
          RiAttributeBegin();
          RiAttributeEnd();
        }).empty(),
        "the outer context still works");
  check(reportsFrom([] { RiEnd(); }).empty(), "RiEnd on the outer context reports nothing");
  check(RiGetContext() == RI_NULL, "RiEnd leaves no context current");
}

void checkFailedRibOpen() {
  std::filesystem::path const directory = "failedbegincapi-nosuchdir";
  std::filesystem::path const file = directory / "failedbegincapi.rib";
  check(!std::filesystem::exists(directory), "the missing directory does not exist");
  std::string const path = file.string();

  checkFailedBegin(reportsFrom([&path] { RiBegin(path.c_str()); }), RIE_BADFILE, "a .rib file that cannot open");
  check(RiGetContext() == RI_NULL, "the failed .rib RiBegin leaves no context current");
  checkNoActiveContext(reportsFrom([] { RiWorldBegin(); }), "RiWorldBegin after the failed .rib RiBegin");
  checkNoActiveContext(reportsFrom([] { RiEnd(); }), "RiEnd after the failed .rib RiBegin");
  check(!std::filesystem::exists(file), "the failed RiBegin writes no file");
}

} // namespace

int main() {
  RiErrorHandler(recordingErrorHandler);

  checkFailedRendererLoad();
  checkFailedRibOpen();

  return checkSummary("a failed C API RiBegin leaves no context current");
}
