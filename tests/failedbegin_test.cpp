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
 * After a RiBegin whose renderer fails to load, the first request that needs
 * a renderer raises RIE_NOTSTARTED at RIE_SEVERE, "No renderer loaded.", and
 * every later one returns without a report or a null dereference. Each
 * RiBegin and each RiEnd clears the record of that report, so each failed
 * load earns exactly one. A failed RiBegin leaves no renderer loaded, whatever
 * preceded it.
 *
 * The C API case runs first: the active context is process-wide, and the C
 * API gives every RiBegin a fresh impl. The impl case drives a second RiBegin
 * on one impl directly.
 */

#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "gmanerror.h"
#include "gmanrendermanimpl.h"
#include "ri.h"

namespace {

struct Raised {
  bool raised = false;
  RtInt code = 0;
  RtInt severity = 0;
  std::string message;
};

template <typename Request> Raised raisedBy(Request request) {
  try {
    request();
  } catch (GMANError const& error) {
    return {true, error.getCode(), error.getSeverity(), error.getMessage()};
  }
  return {};
}

std::vector<Raised> recorded;

RtVoid recordingErrorHandler(RtInt code, RtInt severity, char const* message) {
  recorded.push_back({true, code, severity, message});
}

void checkNoRendererReport(Raised const& report, std::string const& when) {
  check(report.raised, when + " raises");
  check(report.code == RIE_NOTSTARTED, when + " raises RIE_NOTSTARTED");
  check(report.severity == RIE_SEVERE, when + " raises at RIE_SEVERE");
  check(report.message == "No renderer loaded.", when + " says exactly \"No renderer loaded.\"");
}

void checkCApiFailedLoad() {
  RiErrorHandler(recordingErrorHandler);

  recorded.clear();
  RiBegin("gmannosuchrenderer");
  check(recorded.size() == 1, "RiBegin(\"gmannosuchrenderer\") reports once");
  check(!recorded.empty() && recorded[0].code == RIE_SYSTEM, "the failed load reports RIE_SYSTEM");

  recorded.clear();
  RiWorldBegin();
  check(recorded.empty(), "RiWorldBegin after a failed RiBegin reports nothing");

  RiSphere(1, -1, 1, 360, RI_NULL);
  check(recorded.size() == 1, "the first RiSphere reports");
  if (!recorded.empty()) {
    checkNoRendererReport(recorded[0], "the first RiSphere");
  }

  for (int i = 0; i < 999; ++i) {
    RiSphere(1, -1, 1, 360, RI_NULL);
  }
  RtPoint triangle[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  RiPolygon(3, RI_P, triangle, RI_NULL);
  RiWorldEnd();
  RiFormat(8, 8, 1);
  RiEnd();
  check(recorded.size() == 1, "1000 spheres, a polygon, RiWorldEnd, RiFormat and RiEnd report exactly once");
}

void checkImplFailedLoadSecond() {
  std::filesystem::path const image = "failedbegin.tif";
  std::filesystem::remove(image);

  GMANRenderManImpl impl;
  impl.RiBegin(RI_NULL);
  Raised const second = raisedBy([&impl] { impl.RiBegin("gmannosuchrenderer"); });
  check(second.code == RIE_SYSTEM, "a failed second RiBegin raises RIE_SYSTEM");

  impl.RiFormat(8, 8, 1);
  char displayName[] = "failedbegin.tif";
  impl.RiDisplayV(displayName, "file", "rgba", 0, NULL, NULL);
  impl.RiWorldBegin();

  checkNoRendererReport(raisedBy([&impl] { impl.RiSphereV(1, -1, 1, 360, 0, nullptr, nullptr); }),
                        "the first sphere after the failed second RiBegin");
  check(!raisedBy([&impl] { impl.RiSphereV(1, -1, 1, 360, 0, nullptr, nullptr); }).raised,
        "a second sphere returns quietly");
  check(!raisedBy([&impl] { impl.RiWorldEnd(); }).raised, "RiWorldEnd returns quietly");
  check(!std::filesystem::exists(image), "RiWorldEnd writes no image");
  check(!raisedBy([&impl] { impl.RiFormat(8, 8, 1); }).raised, "RiFormat after RiWorldEnd reports nothing");
  check(!raisedBy([&impl] { impl.RiEnd(); }).raised, "RiEnd reports nothing");

  Raised const third = raisedBy([&impl] { impl.RiBegin("gmannosuchrenderer"); });
  check(third.code == RIE_SYSTEM, "a failed RiBegin after RiEnd raises RIE_SYSTEM");

  Raised const outside = raisedBy([&impl] { impl.RiWorldEnd(); });
  check(outside.raised, "RiWorldEnd outside a world raises");
  check(outside.code == RIE_NESTING, "RiWorldEnd outside a world raises RIE_NESTING");
  check(outside.severity == RIE_SEVERE, "RiWorldEnd outside a world raises at RIE_SEVERE");
  check(outside.message == "RiWorldBegin not called",
        "RiWorldEnd outside a world says exactly \"RiWorldBegin not called\"");

  impl.RiWorldBegin();
  checkNoRendererReport(raisedBy([&impl] { impl.RiSphereV(1, -1, 1, 360, 0, nullptr, nullptr); }),
                        "the sphere after a second failed RiBegin");

  Raised const fourth = raisedBy([&impl] { impl.RiBegin("gmannosuchrenderer"); });
  check(fourth.code == RIE_SYSTEM, "a failed RiBegin inside a world raises RIE_SYSTEM");
  checkNoRendererReport(raisedBy([&impl] { impl.RiSphereV(1, -1, 1, 360, 0, nullptr, nullptr); }),
                        "the sphere after a failed RiBegin with no RiEnd between");

  check(!raisedBy([&impl] { impl.RiWorldEnd(); }).raised, "the closing RiWorldEnd returns quietly");
  check(!raisedBy([&impl] { impl.RiEnd(); }).raised, "the closing RiEnd reports nothing");
}

} // namespace

int main() {
  checkCApiFailedLoad();
  checkImplFailedLoadSecond();

  return checkSummary("a failed RiBegin reports RIE_NOTSTARTED once and later requests return quietly");
}
