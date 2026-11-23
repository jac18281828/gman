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

#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "gmanascii.h"
#include "gmanerror.h"
#include "gmanribparse.h"
#include "ri.h"

namespace {

constexpr int MatrixSide = 4;

std::vector<RtFloat> rowMajor(RtMatrix matrix) {
  std::vector<RtFloat> values;
  for (int row = 0; row < MatrixSide; ++row) {
    for (int column = 0; column < MatrixSide; ++column) {
      values.push_back(matrix[row][column]);
    }
  }
  return values;
}

struct BasisCall {
  std::vector<RtFloat> u;
  RtInt ustep = 0;
  std::vector<RtFloat> v;
  RtInt vstep = 0;
};

class MatrixRecordingRenderMan : public GMANASCII {
public:
  std::vector<std::vector<RtFloat>> transforms;
  std::vector<std::vector<RtFloat>> concatTransforms;
  std::vector<BasisCall> bases;
  int spheres = 0;

  RtVoid RiBegin(RtToken) override {}
  RtVoid RiEnd() override {}
  RtVoid RiTransform(RtMatrix transform) override { transforms.push_back(rowMajor(transform)); }
  RtVoid RiConcatTransform(RtMatrix transform) override { concatTransforms.push_back(rowMajor(transform)); }
  RtVoid RiBasis(RtBasis ubasis, RtInt ustep, RtBasis vbasis, RtInt vstep) override {
    bases.push_back({rowMajor(ubasis), ustep, rowMajor(vbasis), vstep});
  }
  RtVoid RiSphereV(RtFloat, RtFloat, RtFloat, RtFloat, RtInt, RtToken[], RtPointer[]) override { ++spheres; }
};

struct Thrown {
  bool threw = false;
  RtInt code = 0;
  RtInt severity = 0;
};

Thrown parseThrows(GMANRIBParse& parser) {
  try {
    parser.parse();
  } catch (GMANError const& error) {
    return {true, error.getCode(), error.getSeverity()};
  }
  return {};
}

std::vector<RtFloat> run(int firstStep) {
  std::vector<RtFloat> values;
  for (int i = 0; i < MatrixSide * MatrixSide; ++i) {
    values.push_back(0.125f * static_cast<RtFloat>(firstStep + i + 1));
  }
  return values;
}

void testWellFormedMatricesArriveInOrder(std::string const& ribDir) {
  const std::string rib = ribDir + "/matrixlength_controls.rib";
  MatrixRecordingRenderMan renderMan;
  Thrown thrown;
  {
    GMANRIBParse parser(renderMan, rib.c_str());
    thrown = parseThrows(parser);
  }

  check(!thrown.threw, "controls: the fixture parses to its end");
  check(renderMan.transforms.size() == 1, "controls: one Transform arrives");
  check(renderMan.concatTransforms.size() == 1, "controls: one ConcatTransform arrives");
  check(renderMan.bases.size() == 1, "controls: one Basis arrives");
  check(renderMan.spheres == 3, "controls: each request is followed by its Sphere");
  if (renderMan.transforms.size() != 1 || renderMan.concatTransforms.size() != 1 || renderMan.bases.size() != 1) {
    return;
  }
  check(renderMan.transforms[0] == run(0), "controls: Transform reads its values in file order");
  check(renderMan.concatTransforms[0] == run(16), "controls: ConcatTransform reads its values in file order");
  check(renderMan.bases[0].u == run(32), "controls: Basis reads its inline u matrix in file order");
  check(renderMan.bases[0].v == run(48), "controls: Basis reads its inline v matrix in file order");
  check(renderMan.bases[0].ustep == 3 && renderMan.bases[0].vstep == 3, "controls: Basis keeps its steps");
}

void checkRejectedBeforeRenderer(std::string const& ribDir, std::string const& name, RtInt code) {
  const std::string rib = ribDir + "/malformed/matrixlength_" + name + ".rib";
  MatrixRecordingRenderMan renderMan;
  Thrown thrown;
  {
    GMANRIBParse parser(renderMan, rib.c_str());
    thrown = parseThrows(parser);
  }

  check(thrown.threw, name + ": the parse throws");
  check(thrown.code == code, name + ": the parse throws the expected error class");
  check(thrown.severity == RIE_ERROR, name + ": the parse throws at RIE_ERROR");
  check(renderMan.transforms.empty(), name + ": no Transform arrives");
  check(renderMan.concatTransforms.empty(), name + ": no ConcatTransform arrives");
  check(renderMan.bases.empty(), name + ": no Basis arrives");
  check(renderMan.spheres == 0, name + ": the parse stops at the request");
}

void testWrongLengthIsAnInconsistency(std::string const& ribDir) {
  for (std::string const name : {"transform_short", "concattransform_long", "basis_u_short", "basis_v_long"}) {
    checkRejectedBeforeRenderer(ribDir, name, RIE_CONSISTENCY);
  }
}

void testWrongTokenIsASyntaxError(std::string const& ribDir) {
  checkRejectedBeforeRenderer(ribDir, "transform_nonnumber", RIE_SYNTAX);
}

} // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <tests/rib dir>\n", argv[0]);
    return 2;
  }
  const std::string ribDir = argv[1];

  testWellFormedMatricesArriveInOrder(ribDir);
  testWrongLengthIsAnInconsistency(ribDir);
  testWrongTokenIsASyntaxError(ribDir);

  return checkSummary("a wrong-length RIB matrix is RIE_CONSISTENCY and a wrong token stays RIE_SYNTAX");
}
