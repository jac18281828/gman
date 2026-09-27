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
 * GMANRIBParse dispatches 14 requests through a dynamic_cast to
 * GMANRenderManImpl, calling the counts-aware overload when it succeeds and
 * the RI-mandated overload on the base GMANRenderMan reference otherwise.
 * Every in-process test in this tree hands the parser a GMANRenderManImpl,
 * so the fallback branch has never run under test. This one hands it a
 * GMANASCII, following tests/pointscount_test.cpp's in-process technique,
 * and reads back every positional argument and parameter the fallback
 * overload of each of the 14 requests received.
 */

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "gmanascii.h"
#include "gmanribparse.h"
#include "ri.h"

namespace {

std::string fmt(RtFloat v) { return std::to_string(v); }
std::string fmt(RtInt v) { return std::to_string(v); }
std::string fmt(const char* v) { return std::string(v); }

template <typename T> void appendArray(std::vector<std::string>& out, T* values, RtInt count) {
  for (RtInt i = 0; i < count; ++i) {
    out.push_back(fmt(values[i]));
  }
}

struct RecordedCall {
  std::string request;
  std::vector<std::string> positional;
  RtInt n = 0;
  std::map<std::string, std::string> params;
};

// Derives from GMANASCII, never from GMANRenderManImpl, so GMANRIBParse's
// dynamic_cast fails and every one of the 14 dispatches falls back to the
// RI-mandated overload on this base. RiBegin/RiEnd are overridden to touch
// no file; every other override records its own positional arguments
// (arrays flattened in signature order) and, for each parameter, its token
// and first value -- every parameter here is an undeclared token, so
// GMANRIBParse's own dictionary lookup falls back to the float path for all
// of them, tokens and "P" included.
class FallbackRenderMan : public GMANASCII {
public:
  std::vector<RecordedCall> calls;

  RtVoid RiBegin(RtToken) override {}
  RtVoid RiEnd() override {}

  RtVoid RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                   RtPointer parms[]) override {
    record("Sphere", {fmt(radius), fmt(zmin), fmt(zmax), fmt(tmax)}, n, tokens, parms);
  }
  RtVoid RiConeV(RtFloat height, RtFloat radius, RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[]) override {
    record("Cone", {fmt(height), fmt(radius), fmt(tmax)}, n, tokens, parms);
  }
  RtVoid RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                     RtPointer parms[]) override {
    record("Cylinder", {fmt(radius), fmt(zmin), fmt(zmax), fmt(tmax)}, n, tokens, parms);
  }
  RtVoid RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax, RtInt n, RtToken tokens[],
                        RtPointer parms[]) override {
    record("Hyperboloid",
           {fmt(point1[0]), fmt(point1[1]), fmt(point1[2]), fmt(point2[0]), fmt(point2[1]), fmt(point2[2]), fmt(tmax)},
           n, tokens, parms);
  }
  RtVoid RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                       RtPointer parms[]) override {
    record("Paraboloid", {fmt(rmax), fmt(zmin), fmt(zmax), fmt(tmax)}, n, tokens, parms);
  }
  RtVoid RiTorusV(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax, RtFloat tmax, RtInt n,
                  RtToken tokens[], RtPointer parms[]) override {
    record("Torus", {fmt(majrad), fmt(minrad), fmt(phimin), fmt(phimax), fmt(tmax)}, n, tokens, parms);
  }
  RtVoid RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax, RtInt n, RtToken tokens[], RtPointer parms[]) override {
    record("Disk", {fmt(height), fmt(radius), fmt(tmax)}, n, tokens, parms);
  }
  RtVoid RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[]) override {
    record("Polygon", {fmt(nverts)}, n, tokens, parms);
  }
  RtVoid RiGeneralPolygonV(RtInt nloops, RtInt nverts[], RtInt n, RtToken tokens[], RtPointer parms[]) override {
    std::vector<std::string> positional = {fmt(nloops)};
    appendArray(positional, nverts, nloops);
    record("GeneralPolygon", positional, n, tokens, parms);
  }
  RtVoid RiPointsPolygonsV(RtInt npolys, RtInt nverts[], RtInt verts[], RtInt n, RtToken tokens[],
                           RtPointer parms[]) override {
    std::vector<std::string> positional = {fmt(npolys)};
    appendArray(positional, nverts, npolys);
    RtInt totalVerts = 0;
    for (RtInt i = 0; i < npolys; ++i) {
      totalVerts += nverts[i];
    }
    appendArray(positional, verts, totalVerts);
    record("PointsPolygons", positional, n, tokens, parms);
  }
  RtVoid RiPointsGeneralPolygonsV(RtInt npolys, RtInt nloops[], RtInt nverts[], RtInt verts[], RtInt n,
                                  RtToken tokens[], RtPointer parms[]) override {
    std::vector<std::string> positional = {fmt(npolys)};
    appendArray(positional, nloops, npolys);
    RtInt totalLoops = 0;
    for (RtInt i = 0; i < npolys; ++i) {
      totalLoops += nloops[i];
    }
    appendArray(positional, nverts, totalLoops);
    RtInt totalVerts = 0;
    for (RtInt i = 0; i < totalLoops; ++i) {
      totalVerts += nverts[i];
    }
    appendArray(positional, verts, totalVerts);
    record("PointsGeneralPolygons", positional, n, tokens, parms);
  }
  RtVoid RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[]) override {
    record("Patch", {fmt(type)}, n, tokens, parms);
  }
  RtVoid RiNuPatchV(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin, RtFloat umax, RtInt nv, RtInt vorder,
                    RtFloat vknot[], RtFloat vmin, RtFloat vmax, RtInt n, RtToken tokens[],
                    RtPointer parms[]) override {
    std::vector<std::string> positional = {fmt(nu), fmt(uorder)};
    appendArray(positional, uknot, nu + uorder);
    positional.push_back(fmt(umin));
    positional.push_back(fmt(umax));
    positional.push_back(fmt(nv));
    positional.push_back(fmt(vorder));
    appendArray(positional, vknot, nv + vorder);
    positional.push_back(fmt(vmin));
    positional.push_back(fmt(vmax));
    record("NuPatch", positional, n, tokens, parms);
  }
  RtVoid RiPatchMeshV(RtToken type, RtInt nu, RtToken uwrap, RtInt nv, RtToken vwrap, RtInt n, RtToken tokens[],
                      RtPointer parms[]) override {
    record("PatchMesh", {fmt(type), fmt(nu), fmt(uwrap), fmt(nv), fmt(vwrap)}, n, tokens, parms);
  }

private:
  // Every parameter's value here is stored through parseParameterList's
  // float path (see the class comment), so its first element reads back as
  // RtFloat regardless of which request carried it.
  void record(std::string request, std::vector<std::string> positional, RtInt n, RtToken tokens[], RtPointer parms[]) {
    RecordedCall call;
    call.request = std::move(request);
    call.positional = std::move(positional);
    call.n = n;
    for (RtInt i = 0; i < n; ++i) {
      call.params[tokens[i]] = fmt(static_cast<RtFloat*>(parms[i])[0]);
    }
    calls.push_back(std::move(call));
  }
};

// Checks one recorded call against its expected shape, matching parameters
// by token rather than by index.
void checkCall(const std::vector<RecordedCall>& calls, std::size_t index, const std::string& request,
               const std::vector<std::string>& positional, const std::map<std::string, std::string>& params) {
  if (index >= calls.size()) {
    check(false, request + ": call " + std::to_string(index) + " arrived");
    return;
  }
  const RecordedCall& call = calls[index];
  check(call.request == request, request + ": request name matches");
  check(call.positional == positional, request + ": positional arguments match, in order");
  check(call.n == (RtInt)params.size(), request + ": parameter count matches");
  check(call.params == params, request + ": every parameter's token and first value matches");
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <tests/rib dir>\n", argv[0]);
    return 2;
  }
  const std::string dir = argv[1];

  FallbackRenderMan renderMan;
  const std::string rib = dir + "/ribparsefallback.rib";
  {
    GMANRIBParse parser(renderMan, rib.c_str());
    parser.parse();
  }

  check(renderMan.calls.size() == 15, "ribparsefallback: 15 calls arrive (the 14 requests, then the closing Sphere)");

  checkCall(renderMan.calls, 0, "Sphere", {"10.000000", "11.000000", "12.000000", "13.000000"},
            {{"sphereData", "100.000000"}});
  checkCall(renderMan.calls, 1, "Cone", {"20.000000", "21.000000", "22.000000"}, {{"coneData", "200.000000"}});
  checkCall(renderMan.calls, 2, "Cylinder", {"30.000000", "31.000000", "32.000000", "33.000000"},
            {{"cylinderData", "300.000000"}});
  checkCall(renderMan.calls, 3, "Hyperboloid",
            {"40.000000", "41.000000", "42.000000", "43.000000", "44.000000", "45.000000", "46.000000"},
            {{"hyperboloidData", "400.000000"}});
  checkCall(renderMan.calls, 4, "Paraboloid", {"50.000000", "51.000000", "52.000000", "53.000000"},
            {{"paraboloidData", "500.000000"}});
  checkCall(renderMan.calls, 5, "Torus", {"60.000000", "61.000000", "62.000000", "63.000000", "64.000000"},
            {{"torusData", "600.000000"}});
  checkCall(renderMan.calls, 6, "Disk", {"70.000000", "71.000000", "72.000000"}, {{"diskData", "700.000000"}});
  checkCall(renderMan.calls, 7, "Polygon", {"3"}, {{"P", "1.000000"}});
  checkCall(renderMan.calls, 8, "GeneralPolygon", {"2", "3", "4"}, {{"generalPolygonData", "900.000000"}});
  checkCall(renderMan.calls, 9, "PointsPolygons", {"1", "4", "0", "1", "2", "3"},
            {{"pointsPolygonsData", "1000.000000"}});
  checkCall(renderMan.calls, 10, "PointsGeneralPolygons", {"1", "2", "3", "4", "0", "1", "2", "3", "4", "5", "6"},
            {{"pointsGeneralPolygonsData", "1100.000000"}});
  checkCall(renderMan.calls, 11, "Patch", {"bilinear"}, {{"patchData", "1200.000000"}});
  checkCall(renderMan.calls, 12, "NuPatch",
            {"5",        "2",         "0.000000",  "0.000000", "1.000000", "1.000000", "2.000000",  "2.000000",
             "3.000000", "10.000000", "11.000000", "6",        "3",        "0.000000", "0.000000",  "0.000000",
             "1.000000", "1.000000",  "2.000000",  "2.000000", "2.000000", "3.000000", "12.000000", "13.000000"},
            {{"nuPatchData", "1400.000000"}});
  checkCall(renderMan.calls, 13, "PatchMesh", {"bicubic", "7", "nonperiodic", "8", "periodic"},
            {{"patchMeshData", "1500.000000"}});
  checkCall(renderMan.calls, 14, "Sphere", {"0.500000", "-0.500000", "0.500000", "360.000000"}, {});

  return checkSummary("GMANRIBParse's fallback dispatch reaches a renderer without supplied counts");
}
