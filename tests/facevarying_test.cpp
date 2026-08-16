/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Step 2: facevarying. include/gmandictionary.h already declared
 * TokenClass{CONSTANT,UNIFORM,VARYING,VERTEX} and GMANDictionary::allocSize
 * already handled all four; this pins the fifth, FACEVARYING, and that its
 * allocation size is the facevarying count -- for a polygon mesh, the total
 * vertex count across all faces -- not the shared (varying) vertex count.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "gmandictionary.h"

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

} // namespace

int main() {
  GMANDictionary dictionary;

  // An inline facevarying declaration, as it would appear in a
  // PointsGeneralPolygons parameter list: "facevarying float foo" [...].
  GMANTokenId id = dictionary.getTokenId("facevarying float foo");

  check(dictionary.getClass(id) == GMANTokenEntry::FACEVARYING,
	"facevarying float foo declares class FACEVARYING");
  check(dictionary.getType(id) == GMANTokenEntry::FLOAT,
	"facevarying float foo declares type FLOAT");

  // A mesh with 4 shared (varying) vertices but 24 facevarying corners --
  // e.g. six quads sharing no UVs across face boundaries.
  const RtInt vertex = 4;
  const RtInt varying = 4;
  const RtInt uniform = 1;
  const RtInt facevarying = 24;

  const int size = dictionary.allocSize(id, vertex, varying, uniform, facevarying);
  check(size == facevarying,
	"allocSize returns the facevarying total, not the varying count");
  check(size != varying, "the facevarying total differs from varying here");

  // A second token, "varying", proves FACEVARYING did not fold into or
  // displace VARYING's own accounting.
  GMANTokenId varyingId = dictionary.getTokenId("varying float bar");
  const int varyingSize = dictionary.allocSize(varyingId, vertex, varying,
						uniform, facevarying);
  check(varyingSize == varying,
	"a plain varying token still allocates by the varying count");

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  std::printf("facevarying holds\n");
  return 0;
}
