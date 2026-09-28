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
 * GMANPolygonMesh and its four factory functions, called directly with
 * arrays and a local GMANDictionary, as the white-box polygon tests build
 * theirs. Covers each request's own normalisation into faces and loops,
 * its parameter-list sizing, every rejection rule, a missing "P", the
 * degenerate shapes the factory still accepts, that a mesh owns its own
 * copies, and the type's own constructibility.
 */

#include <algorithm>
#include <climits>
#include <cstdio>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "check.h"
#include "gmandictionary.h"
#include "gmanlog.h"
#include "gmanpolygonmeshfactory.h"
#include "ri.h"

namespace {

// nverts (or an nverts sum) just above INT_MAX / 3: every request's own
// overflow rule fires here, never on the vertex count itself.
constexpr RtInt kNvertsOverflow = INT_MAX / 3 + 1;

// A point index whose 1 + max(verts), times 3, overflows RtInt.
constexpr RtInt kVertIndexOverflow = INT_MAX;

// pointCount points, each a distinct (x, y, z) so a wrong index or a
// wrong count reads back the wrong triple.
std::vector<RtFloat> makePoints(RtInt pointCount) {
  std::vector<RtFloat> p((std::size_t)pointCount * 3);
  for (RtInt i = 0; i < pointCount; i++) {
    p[(std::size_t)(3 * i)] = (RtFloat)(3 * i);
    p[(std::size_t)(3 * i) + 1] = (RtFloat)(3 * i + 1);
    p[(std::size_t)(3 * i) + 2] = (RtFloat)(3 * i + 2);
  }
  return p;
}

template <typename T> bool spanEquals(std::span<T const> s, std::vector<T> const& expected) {
  return s.size() == expected.size() && std::equal(s.begin(), s.end(), expected.begin());
}

std::size_t countOccurrences(std::string const& haystack, std::string const& needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

// Runs fn with warnings captured to a fresh file at path -- setLogFile
// itself logs one GMAN INFO: line, as checkCreateReturningNullWarns
// (tests/indirectpass_test.cpp) already relies on -- and returns the
// file's contents.
template <typename Fn> std::string captureLog(std::string const& path, Fn&& fn) {
  std::remove(path.c_str());
  setLogFile(path.c_str());
  setScreenOutput(false);

  fn();

  setLogFile("/dev/null");
  setScreenOutput(true);

  std::ifstream in(path, std::ios::binary);
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

void checkPolygon(std::string const& logPath) {
  GMANDictionary dictionary;
  std::vector<RtFloat> p = makePoints(4);
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {(RtPointer)p.data()};

  std::optional<GMANPolygonMesh> mesh;
  captureLog(logPath, [&] { mesh = gman::polygonMesh(4, dictionary, 1, tokens, parms, nullptr); });

  check(mesh.has_value(), "Polygon: builds a mesh");
  if (!mesh.has_value()) {
    return;
  }
  check(mesh->faceCount() == 1, "Polygon: one face");
  check(spanEquals<RtInt>(mesh->face(0), {4}), "Polygon: face(0) is {4}");
  check(spanEquals<RtInt>(mesh->loop(0, 0), {0, 1, 2, 3}), "Polygon: loop(0, 0) is {0, 1, 2, 3}");
  check(spanEquals<RtFloat>(mesh->points(), p), "Polygon: points() equals the 12 floats supplied");
}

void checkGeneralPolygon(std::string const& logPath) {
  GMANDictionary dictionary;
  std::vector<RtInt> const nverts = {4, 3};
  std::vector<RtFloat> p = makePoints(7);
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {(RtPointer)p.data()};

  std::optional<GMANPolygonMesh> mesh;
  captureLog(logPath,
             [&] { mesh = gman::generalPolygonMesh(2, nverts.data(), dictionary, 1, tokens, parms, nullptr); });

  check(mesh.has_value(), "GeneralPolygon: builds a mesh");
  if (!mesh.has_value()) {
    return;
  }
  check(mesh->faceCount() == 1, "GeneralPolygon: one face");
  check(spanEquals<RtInt>(mesh->face(0), {4, 3}), "GeneralPolygon: face(0) is {4, 3}");
  check(spanEquals<RtInt>(mesh->loop(0, 0), {0, 1, 2, 3}), "GeneralPolygon: loop(0, 0) is {0, 1, 2, 3}");
  check(spanEquals<RtInt>(mesh->loop(0, 1), {4, 5, 6}), "GeneralPolygon: loop(0, 1) is {4, 5, 6}");
  check(spanEquals<RtFloat>(mesh->points(), p), "GeneralPolygon: points() holds 21 floats");
}

void checkPointsPolygons(std::string const& logPath) {
  GMANDictionary dictionary;
  // Two faces of 3 and 4 points, sharing points 1 and 2; the largest index
  // is 4, so the point count is 5.
  std::vector<RtInt> const nverts = {3, 4};
  std::vector<RtInt> const verts = {0, 1, 2, 1, 2, 3, 4};
  std::vector<RtFloat> p = makePoints(5);
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {(RtPointer)p.data()};

  std::optional<GMANPolygonMesh> mesh;
  captureLog(logPath, [&] {
    mesh = gman::pointsPolygonsMesh(2, nverts.data(), verts.data(), dictionary, 1, tokens, parms, nullptr);
  });

  check(mesh.has_value(), "PointsPolygons: builds a mesh");
  if (!mesh.has_value()) {
    return;
  }
  check(mesh->faceCount() == 2, "PointsPolygons: two faces");
  check(spanEquals<RtInt>(mesh->face(0), {3}), "PointsPolygons: face(0) is {3}");
  check(spanEquals<RtInt>(mesh->face(1), {4}), "PointsPolygons: face(1) is {4}");
  check(spanEquals<RtInt>(mesh->loop(0, 0), {0, 1, 2}), "PointsPolygons: loop(0, 0) is verts' first slice");
  check(spanEquals<RtInt>(mesh->loop(1, 0), {1, 2, 3, 4}), "PointsPolygons: loop(1, 0) is verts' second slice");
  check(spanEquals<RtFloat>(mesh->points(), p), "PointsPolygons: points() holds 15 floats");
}

void checkPointsGeneralPolygons(std::string const& logPath) {
  GMANDictionary dictionary;
  // Face 0 has an outer ring (4 points) and a hole (3 points); face 1 has
  // one loop (4 points). The largest index is 10, so the point count is
  // 11.
  std::vector<RtInt> const nloops = {2, 1};
  std::vector<RtInt> const nverts = {4, 3, 4};
  std::vector<RtInt> const verts = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  std::vector<RtFloat> p = makePoints(11);
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {(RtPointer)p.data()};

  std::optional<GMANPolygonMesh> mesh;
  captureLog(logPath, [&] {
    mesh = gman::pointsGeneralPolygonsMesh(2, nloops.data(), nverts.data(), verts.data(), dictionary, 1, tokens, parms,
                                           nullptr);
  });

  check(mesh.has_value(), "PointsGeneralPolygons: builds a mesh");
  if (!mesh.has_value()) {
    return;
  }
  check(mesh->faceCount() == 2, "PointsGeneralPolygons: two faces");
  check(mesh->face(0).size() == 2, "PointsGeneralPolygons: face(0) has two entries");
  check(mesh->face(1).size() == 1, "PointsGeneralPolygons: face(1) has one entry");
  check(spanEquals<RtInt>(mesh->face(0), {4, 3}), "PointsGeneralPolygons: face(0) is {4, 3}");
  check(spanEquals<RtInt>(mesh->face(1), {4}), "PointsGeneralPolygons: face(1) is {4}");
  check(spanEquals<RtInt>(mesh->loop(0, 0), {0, 1, 2, 3}), "PointsGeneralPolygons: loop(0, 0) is its run of verts");
  check(spanEquals<RtInt>(mesh->loop(0, 1), {4, 5, 6}), "PointsGeneralPolygons: loop(0, 1) is its run of verts");
  check(spanEquals<RtInt>(mesh->loop(1, 0), {7, 8, 9, 10}), "PointsGeneralPolygons: loop(1, 0) is its run of verts");
}

// The log's "Parameter "<token>": declared length <N>, ..." line for
// token -- the inline declaration exactly as it appears in the request's
// own tokens[], the warning's own key -- once fn (which builds a
// parameter list with a supplied count above every declared length) has
// run.
void checkDeclaredLength(std::string const& log, std::string const& caseName, char const* token, RtInt declared) {
  std::string const needle =
      "Parameter \"" + std::string(token) + "\": declared length " + std::to_string(declared) + ",";
  check(log.find(needle) != std::string::npos,
        "Sizing: " + caseName + " \"" + std::string(token) + "\" declared length " + std::to_string(declared));
}

void checkSizing(std::string const& logPath) {
  // A supplied count above any declared length, and backing arrays at
  // least that long, so every token's own "declared length" line prints.
  RtInt const counts[4] = {1000, 1000, 1000, 1000};
  std::vector<RtFloat> data(1000, 0.0f);
  RtPointer parms[4] = {(RtPointer)data.data(), (RtPointer)data.data(), (RtPointer)data.data(), (RtPointer)data.data()};

  {
    // nverts >= 3, so Polygon's vertex count (3) differs from its
    // facevarying count (1).
    GMANDictionary dictionary;
    RtToken tokens[4] = {"vertex float pvtok", "varying float pvytok", "uniform float putok",
                         "facevarying float pfvtok"};
    std::string const log = captureLog(logPath, [&] { gman::polygonMesh(3, dictionary, 4, tokens, parms, counts); });
    checkDeclaredLength(log, "Polygon", "vertex float pvtok", 3);
    checkDeclaredLength(log, "Polygon", "varying float pvytok", 3);
    checkDeclaredLength(log, "Polygon", "uniform float putok", 1);
    checkDeclaredLength(log, "Polygon", "facevarying float pfvtok", 1);
  }
  {
    // nloops {4, 3}, sum 7: vertex, varying and facevarying all read 7.
    GMANDictionary dictionary;
    std::vector<RtInt> const nverts = {4, 3};
    RtToken tokens[4] = {"vertex float gvtok", "varying float gvytok", "uniform float gutok",
                         "facevarying float gfvtok"};
    std::string const log =
        captureLog(logPath, [&] { gman::generalPolygonMesh(2, nverts.data(), dictionary, 4, tokens, parms, counts); });
    checkDeclaredLength(log, "GeneralPolygon", "vertex float gvtok", 7);
    checkDeclaredLength(log, "GeneralPolygon", "varying float gvytok", 7);
    checkDeclaredLength(log, "GeneralPolygon", "uniform float gutok", 1);
    checkDeclaredLength(log, "GeneralPolygon", "facevarying float gfvtok", 7);
  }
  {
    // npolys 2, nverts {2, 3} (facevarying 5), verts reaching 6 (vertex
    // 7): point count, npolys and the facevarying sum all differ.
    GMANDictionary dictionary;
    std::vector<RtInt> const nverts = {2, 3};
    std::vector<RtInt> const verts = {0, 1, 2, 3, 6};
    RtToken tokens[4] = {"vertex float ptvtok", "varying float ptvytok", "uniform float ptutok",
                         "facevarying float ptfvtok"};
    std::string const log = captureLog(logPath, [&] {
      gman::pointsPolygonsMesh(2, nverts.data(), verts.data(), dictionary, 4, tokens, parms, counts);
    });
    checkDeclaredLength(log, "PointsPolygons", "vertex float ptvtok", 7);
    checkDeclaredLength(log, "PointsPolygons", "varying float ptvytok", 7);
    checkDeclaredLength(log, "PointsPolygons", "uniform float ptutok", 2);
    checkDeclaredLength(log, "PointsPolygons", "facevarying float ptfvtok", 5);
  }
  {
    // npolys 2, nloops {1, 1}, nverts {2, 3} (facevarying 5), verts
    // reaching 6 (vertex 7): the same three-way distinction as
    // PointsPolygons.
    GMANDictionary dictionary;
    std::vector<RtInt> const nloops = {1, 1};
    std::vector<RtInt> const nverts = {2, 3};
    std::vector<RtInt> const verts = {0, 1, 2, 3, 6};
    RtToken tokens[4] = {"vertex float pgvtok", "varying float pgvytok", "uniform float pgutok",
                         "facevarying float pgfvtok"};
    std::string const log = captureLog(logPath, [&] {
      gman::pointsGeneralPolygonsMesh(2, nloops.data(), nverts.data(), verts.data(), dictionary, 4, tokens, parms,
                                      counts);
    });
    checkDeclaredLength(log, "PointsGeneralPolygons", "vertex float pgvtok", 7);
    checkDeclaredLength(log, "PointsGeneralPolygons", "varying float pgvytok", 7);
    checkDeclaredLength(log, "PointsGeneralPolygons", "uniform float pgutok", 2);
    checkDeclaredLength(log, "PointsGeneralPolygons", "facevarying float pgfvtok", 5);
  }
}

// Runs call with warnings captured, and asserts it returns std::nullopt,
// logs message and no other warning.
template <typename Fn>
void checkRejection(std::string const& logPath, std::string const& name, std::string const& message, Fn&& call) {
  std::optional<GMANPolygonMesh> result;
  std::string const log = captureLog(logPath, [&] { result = call(); });
  check(!result.has_value(), "Rejections: " + name + " returns std::nullopt");
  check(log.find(message) != std::string::npos, "Rejections: " + name + " logs its message");
  check(countOccurrences(log, "GMAN WARNING:") == 1, "Rejections: " + name + " logs no other warning");
}

void checkRejections(std::string const& logPath) {
  GMANDictionary dictionary;

  // Polygon: 2 rules.
  checkRejection(logPath, "Polygon negative nverts", "Polygon: nverts[0] = -1 is negative; ignoring.",
                 [&] { return gman::polygonMesh(-1, dictionary, 0, nullptr, nullptr, nullptr); });
  checkRejection(logPath, "Polygon nverts overflow",
                 "Polygon: nverts sums to " + std::to_string(kNvertsOverflow) + ", times 3 overflows RtInt; ignoring.",
                 [&] { return gman::polygonMesh(kNvertsOverflow, dictionary, 0, nullptr, nullptr, nullptr); });

  // GeneralPolygon: 3 rules.
  checkRejection(logPath, "GeneralPolygon invalid nloops", "GeneralPolygon: nloops = 0 is invalid; ignoring.",
                 [&] { return gman::generalPolygonMesh(0, nullptr, dictionary, 0, nullptr, nullptr, nullptr); });
  {
    std::vector<RtInt> const nverts = {-1};
    checkRejection(
        logPath, "GeneralPolygon negative nverts", "GeneralPolygon: nverts[0] = -1 is negative; ignoring.",
        [&] { return gman::generalPolygonMesh(1, nverts.data(), dictionary, 0, nullptr, nullptr, nullptr); });
  }
  {
    std::vector<RtInt> const nverts = {kNvertsOverflow};
    checkRejection(
        logPath, "GeneralPolygon nverts overflow",
        "GeneralPolygon: nverts sums to " + std::to_string(kNvertsOverflow) + ", times 3 overflows RtInt; ignoring.",
        [&] { return gman::generalPolygonMesh(1, nverts.data(), dictionary, 0, nullptr, nullptr, nullptr); });
  }

  // PointsPolygons: 5 rules.
  checkRejection(logPath, "PointsPolygons invalid npolys", "PointsPolygons: npolys = -1 is invalid; ignoring.", [&] {
    return gman::pointsPolygonsMesh(-1, nullptr, nullptr, dictionary, 0, nullptr, nullptr, nullptr);
  });
  {
    std::vector<RtInt> const nverts = {-1};
    std::vector<RtInt> const verts = {};
    checkRejection(
        logPath, "PointsPolygons negative nverts", "PointsPolygons: nverts[0] = -1 is negative; ignoring.", [&] {
          return gman::pointsPolygonsMesh(1, nverts.data(), verts.data(), dictionary, 0, nullptr, nullptr, nullptr);
        });
  }
  {
    std::vector<RtInt> const nverts = {kNvertsOverflow};
    std::vector<RtInt> const verts = {};
    checkRejection(
        logPath, "PointsPolygons nverts overflow",
        "PointsPolygons: nverts sums to " + std::to_string(kNvertsOverflow) + ", times 3 overflows RtInt; ignoring.",
        [&] {
          return gman::pointsPolygonsMesh(1, nverts.data(), verts.data(), dictionary, 0, nullptr, nullptr, nullptr);
        });
  }
  {
    std::vector<RtInt> const nverts = {1};
    std::vector<RtInt> const verts = {-1};
    checkRejection(
        logPath, "PointsPolygons negative verts", "PointsPolygons: verts[0] = -1 is negative; ignoring.", [&] {
          return gman::pointsPolygonsMesh(1, nverts.data(), verts.data(), dictionary, 0, nullptr, nullptr, nullptr);
        });
  }
  {
    std::vector<RtInt> const nverts = {1};
    std::vector<RtInt> const verts = {kVertIndexOverflow};
    checkRejection(logPath, "PointsPolygons vertex count overflow",
                   "PointsPolygons: vertex count " + std::to_string((long long)kVertIndexOverflow + 1) +
                       " (1 + max(verts)), times 3 overflows RtInt; ignoring.",
                   [&] {
                     return gman::pointsPolygonsMesh(1, nverts.data(), verts.data(), dictionary, 0, nullptr, nullptr,
                                                     nullptr);
                   });
  }
  {
    // Breaks two rules: nverts[1] is negative and verts[0] is negative
    // too; only the nverts message appears, since the nverts rule runs
    // first and verts is never read once it fires.
    std::vector<RtInt> const nverts = {2, -1, 3};
    std::vector<RtInt> const verts = {-1, 0, 1, 2, 3};
    std::optional<GMANPolygonMesh> result;
    std::string const log = captureLog(logPath, [&] {
      result = gman::pointsPolygonsMesh(3, nverts.data(), verts.data(), dictionary, 0, nullptr, nullptr, nullptr);
    });
    check(!result.has_value(), "Rejections: PointsPolygons nverts precedes verts returns std::nullopt");
    check(log.find("PointsPolygons: nverts[1] = -1 is negative; ignoring.") != std::string::npos,
          "Rejections: PointsPolygons nverts precedes verts logs the nverts message");
    check(log.find("verts[0]") == std::string::npos,
          "Rejections: PointsPolygons nverts precedes verts logs no verts message");
  }

  // PointsGeneralPolygons: 7 rules.
  checkRejection(
      logPath, "PointsGeneralPolygons invalid npolys", "PointsGeneralPolygons: npolys = -1 is invalid; ignoring.", [&] {
        return gman::pointsGeneralPolygonsMesh(-1, nullptr, nullptr, nullptr, dictionary, 0, nullptr, nullptr, nullptr);
      });
  {
    std::vector<RtInt> const nloops = {0};
    checkRejection(logPath, "PointsGeneralPolygons invalid nloops",
                   "PointsGeneralPolygons: nloops[0] = 0 is invalid; ignoring.", [&] {
                     return gman::pointsGeneralPolygonsMesh(1, nloops.data(), nullptr, nullptr, dictionary, 0, nullptr,
                                                            nullptr, nullptr);
                   });
  }
  {
    std::vector<RtInt> const nloops = {INT_MAX, 1};
    checkRejection(logPath, "PointsGeneralPolygons nloops overflow",
                   "PointsGeneralPolygons: nloops sums to " + std::to_string((long long)INT_MAX + 1) +
                       ", overflows RtInt; ignoring.",
                   [&] {
                     return gman::pointsGeneralPolygonsMesh(2, nloops.data(), nullptr, nullptr, dictionary, 0, nullptr,
                                                            nullptr, nullptr);
                   });
  }
  {
    std::vector<RtInt> const nloops = {1};
    std::vector<RtInt> const nverts = {-1};
    checkRejection(logPath, "PointsGeneralPolygons negative nverts",
                   "PointsGeneralPolygons: nverts[0] = -1 is negative; ignoring.", [&] {
                     return gman::pointsGeneralPolygonsMesh(1, nloops.data(), nverts.data(), nullptr, dictionary, 0,
                                                            nullptr, nullptr, nullptr);
                   });
  }
  {
    std::vector<RtInt> const nloops = {1};
    std::vector<RtInt> const nverts = {kNvertsOverflow};
    checkRejection(logPath, "PointsGeneralPolygons nverts overflow",
                   "PointsGeneralPolygons: nverts sums to " + std::to_string(kNvertsOverflow) +
                       ", times 3 overflows RtInt; ignoring.",
                   [&] {
                     return gman::pointsGeneralPolygonsMesh(1, nloops.data(), nverts.data(), nullptr, dictionary, 0,
                                                            nullptr, nullptr, nullptr);
                   });
  }
  {
    std::vector<RtInt> const nloops = {1};
    std::vector<RtInt> const nverts = {1};
    std::vector<RtInt> const verts = {-1};
    checkRejection(logPath, "PointsGeneralPolygons negative verts",
                   "PointsGeneralPolygons: verts[0] = -1 is negative; ignoring.", [&] {
                     return gman::pointsGeneralPolygonsMesh(1, nloops.data(), nverts.data(), verts.data(), dictionary,
                                                            0, nullptr, nullptr, nullptr);
                   });
  }
  {
    std::vector<RtInt> const nloops = {1};
    std::vector<RtInt> const nverts = {1};
    std::vector<RtInt> const verts = {kVertIndexOverflow};
    checkRejection(logPath, "PointsGeneralPolygons vertex count overflow",
                   "PointsGeneralPolygons: vertex count " + std::to_string((long long)kVertIndexOverflow + 1) +
                       " (1 + max(verts)), times 3 overflows RtInt; ignoring.",
                   [&] {
                     return gman::pointsGeneralPolygonsMesh(1, nloops.data(), nverts.data(), verts.data(), dictionary,
                                                            0, nullptr, nullptr, nullptr);
                   });
  }
}

void checkMissingP(std::string const& logPath) {
  GMANDictionary dictionary;

  auto checkNoWarning = [&](std::string const& name, std::optional<GMANPolygonMesh> result, std::string const& log) {
    check(!result.has_value(), "Missing \"P\": " + name + " returns std::nullopt");
    check(log.find("GMAN WARNING:") == std::string::npos, "Missing \"P\": " + name + " logs no warning");
  };

  {
    std::optional<GMANPolygonMesh> result;
    std::string const log =
        captureLog(logPath, [&] { result = gman::polygonMesh(3, dictionary, 0, nullptr, nullptr, nullptr); });
    checkNoWarning("Polygon", result, log);
  }
  {
    std::vector<RtInt> const nverts = {3};
    std::optional<GMANPolygonMesh> result;
    std::string const log = captureLog(logPath, [&] {
      result = gman::generalPolygonMesh(1, nverts.data(), dictionary, 0, nullptr, nullptr, nullptr);
    });
    checkNoWarning("GeneralPolygon", result, log);
  }
  {
    std::vector<RtInt> const nverts = {3};
    std::vector<RtInt> const verts = {0, 1, 2};
    std::optional<GMANPolygonMesh> result;
    std::string const log = captureLog(logPath, [&] {
      result = gman::pointsPolygonsMesh(1, nverts.data(), verts.data(), dictionary, 0, nullptr, nullptr, nullptr);
    });
    checkNoWarning("PointsPolygons", result, log);
  }
  {
    std::vector<RtInt> const nloops = {1};
    std::vector<RtInt> const nverts = {3};
    std::vector<RtInt> const verts = {0, 1, 2};
    std::optional<GMANPolygonMesh> result;
    std::string const log = captureLog(logPath, [&] {
      result = gman::pointsGeneralPolygonsMesh(1, nloops.data(), nverts.data(), verts.data(), dictionary, 0, nullptr,
                                               nullptr, nullptr);
    });
    checkNoWarning("PointsGeneralPolygons", result, log);
  }
}

void checkDegenerate(std::string const& logPath) {
  GMANDictionary dictionary;

  {
    std::vector<RtFloat> p = makePoints(2);
    RtToken tokens[1] = {RI_P};
    RtPointer parms[1] = {(RtPointer)p.data()};
    std::optional<GMANPolygonMesh> mesh;
    captureLog(logPath, [&] { mesh = gman::polygonMesh(2, dictionary, 1, tokens, parms, nullptr); });
    check(mesh.has_value(), "Degenerate: Polygon nverts 2 still builds a mesh");
    if (mesh.has_value()) {
      check(spanEquals<RtInt>(mesh->loop(0, 0), {0, 1}), "Degenerate: Polygon nverts 2's loop(0, 0) is {0, 1}");
    }
  }
  {
    std::vector<RtFloat> p;
    RtToken tokens[1] = {RI_P};
    RtPointer parms[1] = {(RtPointer)p.data()};
    std::optional<GMANPolygonMesh> mesh;
    captureLog(logPath, [&] { mesh = gman::polygonMesh(0, dictionary, 1, tokens, parms, nullptr); });
    check(mesh.has_value(), "Degenerate: Polygon nverts 0 with an empty \"P\" still builds a mesh");
    if (mesh.has_value()) {
      check(mesh->faceCount() == 1, "Degenerate: Polygon nverts 0 has one face");
      check(mesh->loop(0, 0).empty(), "Degenerate: Polygon nverts 0's loop is empty");
    }
  }
}

void checkCopies(std::string const& logPath) {
  GMANDictionary dictionary;
  std::vector<RtInt> nverts = {3, 2};
  std::vector<RtInt> verts = {0, 1, 2, 1, 3};
  std::vector<RtFloat> p = makePoints(4);
  RtToken tokens[1] = {RI_P};
  RtPointer parms[1] = {(RtPointer)p.data()};

  std::optional<GMANPolygonMesh> mesh;
  captureLog(logPath, [&] {
    mesh = gman::pointsPolygonsMesh(2, nverts.data(), verts.data(), dictionary, 1, tokens, parms, nullptr);
  });
  check(mesh.has_value(), "Copies: setup mesh builds");
  if (!mesh.has_value()) {
    return;
  }

  // Overwrites the caller's own arrays in place, at the same size so
  // nothing reallocates: the mesh must not be reading through these
  // pointers.
  std::fill(nverts.begin(), nverts.end(), -1);
  std::fill(verts.begin(), verts.end(), -1);

  check(spanEquals<RtInt>(mesh->face(0), {3}), "Copies: face(0) survives the caller's overwrite");
  check(spanEquals<RtInt>(mesh->loop(0, 0), {0, 1, 2}), "Copies: loop(0, 0) survives the caller's overwrite");
  check(spanEquals<RtInt>(mesh->loop(1, 0), {1, 3}), "Copies: loop(1, 0) survives the caller's overwrite");

  std::optional<GMANPolygonMesh> copy = mesh;
  mesh.reset();
  check(copy.has_value(), "Copies: the copy holds a value");
  if (copy.has_value()) {
    check(spanEquals<RtInt>(copy->loop(0, 0), {0, 1, 2}),
          "Copies: a copy's loop(0, 0) survives the original's destruction");
  }
}

void checkType() {
  static_assert(!std::is_default_constructible_v<GMANPolygonMesh>,
                "Type: GMANPolygonMesh is not default-constructible");
  static_assert(std::is_copy_constructible_v<GMANPolygonMesh>, "Type: GMANPolygonMesh is copy-constructible");
  static_assert(std::is_move_constructible_v<GMANPolygonMesh>, "Type: GMANPolygonMesh is move-constructible");
  check(true, "Type: GMANPolygonMesh is not default-constructible, and is copy- and move-constructible");
}

} // namespace

int main() {
  std::string const logPath = "polygonmesh.log";

  checkPolygon(logPath);
  checkGeneralPolygon(logPath);
  checkPointsPolygons(logPath);
  checkPointsGeneralPolygons(logPath);
  checkSizing(logPath);
  checkRejections(logPath);
  checkMissingP(logPath);
  checkDegenerate(logPath);
  checkCopies(logPath);
  checkType();

  return checkSummary("GMANPolygonMesh and its factory functions hold");
}
