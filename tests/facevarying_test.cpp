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
 *
 * Step 4: the other four storage classes, so all five are pinned in one
 * place rather than four fifths of them being implicit. Each uses a
 * distinct vertex/varying/uniform/facevarying count so a class reading
 * the wrong parameter (e.g. VERTEX using the uniform count) fails loudly
 * instead of by coincidence.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"
#include "gmandictionary.h"

namespace {

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

  // A mesh with distinct counts for every storage class, so a class that
  // reads the wrong one fails on a size mismatch, not by coincidence:
  // 9 tessellated vertices, 4 shared (varying) corners, 6 faces (uniform),
  // 24 facevarying corners -- e.g. six quads sharing no UVs across face
  // boundaries.
  const RtInt vertex = 9;
  const RtInt varying = 4;
  const RtInt uniform = 6;
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

  // The remaining three storage classes: VERTEX (per-tessellated-vertex),
  // UNIFORM (per-face), and CONSTANT (exactly one value regardless of any
  // count -- allocSize's own switch has no multiplier for it at all).
  GMANTokenId vertexId = dictionary.getTokenId("vertex float baz");
  check(dictionary.allocSize(vertexId, vertex, varying, uniform, facevarying) ==
	    vertex,
	"a vertex token allocates by the vertex count");

  GMANTokenId uniformId = dictionary.getTokenId("uniform float qux");
  check(dictionary.allocSize(uniformId, vertex, varying, uniform, facevarying) ==
	    uniform,
	"a uniform token allocates by the uniform count");

  GMANTokenId constantId = dictionary.getTokenId("constant float quux");
  check(dictionary.allocSize(constantId, vertex, varying, uniform, facevarying) ==
	    1,
	"a constant token allocates exactly one value regardless of counts");

  // A constant point (3 floats) proves the type-size multiplier still
  // applies even though the class multiplier is 1 -- allocSize's own
  // "size *= quantity" runs after the class switch, for every class.
  GMANTokenId constantPointId = dictionary.getTokenId("constant point corner");
  check(dictionary.allocSize(constantPointId, vertex, varying, uniform,
			      facevarying) == 3,
	"a constant point still allocates 3 floats (the type size), not 1");

  return checkSummary("facevarying holds");
}
