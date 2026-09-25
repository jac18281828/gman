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
 * GMANSubdivisionTag's own copy constructor and operator= are unreachable
 * from any current call site and so stay untested directly here; CI's
 * sanitizers leg runs this test under ASan, instrumenting every
 * construction and destruction GMANSubdivisionMesh's copy path performs
 * below, not only the ones called directly.
 */

#include <vector>

#include "check.h"
#include "gmanparameterlist.h"
#include "gmanprimitives.h"

namespace {

// Exposes GMANSubdivisionMesh's protected tag array to the test through
// inherited access, with no new friend on the production class.
class TestSubdivisionMesh : public GMANSubdivisionMesh {
public:
  using GMANSubdivisionMesh::GMANSubdivisionMesh;

  RtInt tagCount() const { return ntags; }
  GMANSubdivisionTag const& tagAt(RtInt i) const { return *tags[i]; }
};

void checkTags(TestSubdivisionMesh const& mesh, std::string const& label) {
  check(mesh.tagCount() == 2, label + ": mesh carries two tags");

  GMANSubdivisionTag const& crease = mesh.tagAt(0);
  check(std::string(crease.name()) == "crease", label + ": tag 0 name round-trips");
  const std::vector<RtInt> expectedCreaseInt = {2, 5};
  const std::vector<RtFloat> expectedCreaseFloat = {1.5f, 2.5f, 3.5f};
  check(crease.intArgs() == expectedCreaseInt, label + ": tag 0 int args round-trip");
  check(crease.floatArgs() == expectedCreaseFloat, label + ": tag 0 float args round-trip");

  GMANSubdivisionTag const& hole = mesh.tagAt(1);
  check(std::string(hole.name()) == "hole", label + ": tag 1 name round-trips");
  check(hole.intArgs().empty(), label + ": tag 1 int args stay zero-length");
  const std::vector<RtFloat> expectedHoleFloat = {10.25f, -3.75f};
  check(hole.floatArgs() == expectedHoleFloat, label + ": tag 1 float args round-trip");
}

} // namespace

int main() {
  RtInt nverts[] = {4};
  RtInt verts[] = {0, 1, 2, 3};
  RtToken tags[] = {"crease", "hole"};
  RtInt numargs[] = {2, 3, 0, 2};
  RtInt iargs[] = {2, 5};
  RtFloat fargs[] = {1.5f, 2.5f, 3.5f, 10.25f, -3.75f};
  GMANParameterList pl;

  TestSubdivisionMesh first("catmullclark", 1, nverts, verts, 2, tags, numargs, iargs, fargs, pl);
  checkTags(first, "first");

  TestSubdivisionMesh second(first);
  checkTags(second, "copy-constructed");

  TestSubdivisionMesh third(first);
  third = second;
  checkTags(third, "assigned");

  return checkSummary("subdivisiontag holds");
}
