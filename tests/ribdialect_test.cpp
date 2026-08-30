/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Phase 2's RIB-dialect coverage: the corpus test, the per-request
 * parameter-consumption tests, and the unknown-request skip test.
 *
 * Each request test follows AGENTS.md's "RIB authoring" section, "The
 * desync convention."
 */

#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"

namespace {

struct Result {
  int exitStatus;
  std::string output;
};

// Display writes relative to gman's cwd, which is this test's own
// WORKING_DIRECTORY (see tests/CMakeLists.txt) -- so a plain relative open
// finds whatever the run above wrote, without threading the scratch path
// through every call site.
bool nonEmptyFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  return in.good() && in.tellg() > 0;
}

Result run(const std::string &gman, const std::string &rib, bool debug) {
  const std::string command = "\"" + gman + "\" " + (debug ? "-d " : "") +
    "\"" + rib + "\" 2>&1";

  std::FILE *pipe = popen(command.c_str(), "r");
  Result result{-1, ""};
  if (pipe == nullptr) {
    return result;
  }

  char buffer[512];
  while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
    result.output += buffer;
  }

  const int closeStatus = pclose(pipe);
  result.exitStatus = WIFEXITED(closeStatus) ? WEXITSTATUS(closeStatus) : -1;
  return result;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <gman-binary> <tests/rib dir>\n", argv[0]);
    return 2;
  }

  const std::string gman = argv[1];
  const std::string ribDir = argv[2];

  // Every RISpec 3.2 request phase 2 added parse-and-ignore coverage for,
  // plus pixelfilter.rib -- phase 2's list missed PixelFilter outright (zero
  // hits in the tokenizer), which is defect 2 -- and
  // geometricapproximation.rib, whose keyword this commit also adds to the
  // tokenizer. ifelse.rib and solid.rib each cover a pair of requests that
  // only mean anything together (IfBegin/ElseIf/Else/IfEnd,
  // SolidBegin/SolidEnd).
  const std::vector<std::string> requestFixtures = {
    "curves.rib", "blobby.rib", "subdivisionmesh.rib", "procedural.rib",
    "solid.rib", "detail.rib", "detailrange.rib", "relativedetail.rib",
    "skew.rib", "matte.rib", "trimcurve.rib", "errorhandler.rib",
    "archiverecord.rib", "maketexture.rib", "makebump.rib",
    "makelatlongenvironment.rib", "makecubefaceenvironment.rib",
    "makeshadow.rib", "ifelse.rib", "pixelfilter.rib",
    "geometricapproximation.rib",
  };

  for (const std::string &fixture : requestFixtures) {
    const std::string path = ribDir + "/requests/" + fixture;
    Result r = run(gman, path, /*debug=*/true);
    check(r.exitStatus == 0, fixture + ": gman exits 0");
    check(r.output.find("Keyword token: Sphere") != std::string::npos,
	  fixture + ": the Sphere after it still parses (no desync)");
    // Exit 0 and a parsed Sphere are also what an *unrecognized* request
    // produces, since the front end warns and skips those by design. Without
    // this line, deleting a keyword from the tokenizer leaves the whole
    // table green.
    check(r.output.find("Unrecognized keyword") == std::string::npos,
	  fixture + ": the tokenizer recognizes the request");
  }

  // Defect 1: a token that runs to end of input with no trailing delimiter
  // used to have its last character duplicated. parseKeyword's loop checked
  // eof() *before* the read that could set it, so the failing read at true
  // end of input left its char argument untouched -- still holding the
  // previous iteration's already-consumed character -- and the loop
  // appended it again. Reproduces three ways: a top-level file, a plain
  // ReadArchive target, and a gzip'd one.
  {
    const std::string path = ribDir + "/nonewline/top.rib";
    std::remove("nonewline_top.tif");
    Result r = run(gman, path, /*debug=*/true);
    check(r.exitStatus == 0, "no trailing newline: top-level file exits 0");
    check(r.output.find("Unrecognized keyword") == std::string::npos,
	  "no trailing newline: WorldEnd does not become WorldEndd");
    check(nonEmptyFile("nonewline_top.tif"),
	  "no trailing newline: top-level file still writes an image");
  }
  {
    const std::string path = ribDir + "/nonewline/parent.rib";
    Result r = run(gman, path, /*debug=*/true);
    check(r.exitStatus == 0,
	  "no trailing newline: plain archive target exits 0");
    check(r.output.find("Unrecognized keyword") == std::string::npos,
	  "no trailing newline: AttributeEnd does not become AttributeEndd "
	  "(plain archive)");
    check(r.output.find("Keyword token: Sphere") != std::string::npos,
	  "no trailing newline: the Sphere after the archive still parses");
  }
  {
    const std::string path = ribDir + "/nonewline/parent_gz.rib";
    Result r = run(gman, path, /*debug=*/true);
    check(r.exitStatus == 0,
	  "no trailing newline: gzip'd archive target exits 0");
    check(r.output.find("Unrecognized keyword") == std::string::npos,
	  "no trailing newline: AttributeEnd does not become AttributeEndd "
	  "(gzip'd archive)");
    check(r.output.find("Keyword token: Sphere") != std::string::npos,
	  "no trailing newline: the Sphere after the gzip'd archive still "
	  "parses");
  }

  // The corpus test. tests/rib/corpus/menger.rib is real third-party RIB
  // (Aqsis's own example set -- see tests/rib/README), not authored for
  // this test. Revert step 4 (Procedural, which it uses) or step 7 (array
  // parameters, the "fov" form Projection here uses) and this fails.
  //
  // It does NOT exercise step 1: every request menger.rib uses is one GMAN
  // already recognized, so unknown-request recovery never fires. Step 1 is
  // covered by the unknownrequest.rib fixture below. An earlier version of
  // this comment claimed otherwise -- verified false by reverting step 1 and
  // observing menger.rib still exit 0.
  //
  // It also doubles as defect 3's proof: menger.rib's second line is
  // `Display "+menger.tif" "framebuffer" "rgb"`, and GMAN used to let the
  // last Display win outright, so the unsupported framebuffer driver
  // replaced the working file display and nothing was ever written.
  // Honoring RISpec's '+' prefix (add, don't replace) means the file
  // display survives and menger.tif appears.
  {
    const std::string corpus = ribDir + "/corpus/menger.rib";
    std::remove("menger.tif");
    Result r = run(gman, corpus, /*debug=*/false);
    check(r.exitStatus == 0, "corpus: menger.rib parses to completion, exit 0");
    check(r.output.find("ERROR") == std::string::npos,
	  "corpus: no error reported");
    check(nonEmptyFile("menger.tif"),
	  "corpus: the file display survives the later framebuffer Display "
	  "(defect 3)");
  }

  // ReadArchive of a gzip'd child -- steps 5 and 6 composed, which no other
  // fixture covers (readarchive_test.cpp uses plain files, gzip_test.cpp a
  // gzip'd top-level file). This is also defect 1's payoff: bikeData.rib.gz
  // ends TransformEnd with no trailing newline, which used to corrupt into
  // TransformEndd, leaving the block unclosed and killing the parse on
  // RIE_NESTING partway through the archive's ~5,300 lines -- exactly the
  // shape SPEC.md S8 recorded. Fixed, the whole archive parses and gman
  // reaches exit 0.
  //
  // bike.rib reads bikeData.rib.gz. openRibStream decompresses the archive
  // whole before parsing begins, so reaching any token inside it exercises
  // all of step 6; TransformBegin is the archive's first request and appears
  // nowhere in bike.rib itself, which is what makes it evidence rather than
  // coincidence.
  //
  // The image bike.rib writes is blank -- every pixel the background color.
  // That is a separate, pre-existing defect, confirmed present on
  // unmodified d403afa with a minimal single-Patch RIB with no archive, no
  // gzip and no near-clip precision concern (SPEC.md S8's other open
  // defect): Patch rasterizes no pixels, Sphere in the same scene does. All
  // 5,216 Patch requests in bikeData.rib.gz reach the parser and the
  // renderer with no warnings, so it is not a RIB-front-end gap and out of
  // this task's scope -- reported, not fixed.
  //
  // Under -fsanitize=address (build-debug, the "sanitizers" CI leg), gman
  // aborts instead of exiting 0. bikeData.rib.gz calls `Surface "plastic"`
  // repeatedly; GMANLoadableShader (gmanloadable.cpp) dlopens libplastic.so
  // fresh on every call with no cache, and reaching a *second* call -- only
  // possible now that defect 1 no longer kills the parse first -- makes
  // ASan's ODR checker see two copies of GMANPlastic's vtable and abort.
  // Also pre-existing (any two `Surface "plastic"` requests in one scene
  // reproduce it) and out of scope (gmanloadable.cpp/gmanattributes.cpp,
  // not this task's files). The check below accepts exactly that one
  // known failure shape and nothing else, so an unrelated regression here
  // still fails it.
  {
    const std::string bike = ribDir + "/corpus/bike.rib";
    std::remove("bike.tif");
    Result r = run(gman, bike, /*debug=*/true);
    check(r.output.find("Keyword token: ReadArchive") != std::string::npos,
	  "corpus: bike.rib reaches its ReadArchive");
    check(r.output.find("Keyword token: TransformBegin") != std::string::npos,
	  "corpus: the gzip'd archive decompresses and its requests reach the "
	  "parser");
    if (r.exitStatus == 0) {
      check(nonEmptyFile("bike.tif"),
	    "corpus: bike.rib now parses to completion and writes a "
	    "non-empty image (defect 1)");
    } else {
      check(r.output.find("odr-violation") != std::string::npos &&
	    r.output.find("GMANPlastic") != std::string::npos,
	    "corpus: bike.rib's only non-zero-exit failure left is the known "
	    "libplastic dlopen ODR violation under AddressSanitizer, not a "
	    "RIB-front-end regression");
    }
  }

  // Step 1: an unrecognized request -- Bxdf, a RIS-era request GMAN
  // deliberately does not implement (SPEC.md S4) -- warns once by name and
  // the parse continues past it.
  {
    const std::string path = ribDir + "/unknownrequest.rib";
    Result r = run(gman, path, /*debug=*/false);
    check(r.exitStatus == 0, "unknown request: gman exits 0");
    check(r.output.find("skipping unrecognized request") != std::string::npos,
	  "unknown request: warns once");
    check(r.output.find("Bxdf") != std::string::npos,
	  "unknown request: names the request in the warning");
    check(r.output.find("unrecognized requests skipped: Bxdf") != std::string::npos,
	  "unknown request: reported once more in the end-of-parse summary");
  }

  // Malformed input, for the error paths. A parameter list that throws
  // part-built used to strand both the strings already duplicated into the
  // array and the keys/values the list had accumulated; both are now
  // released as the stack unwinds.
  //
  // What this DOES assert: the fault is detected and named. What it does NOT
  // assert: leak-freedom. gman runs here as a subprocess, so LeakSanitizer
  // aborting it is indistinguishable from the non-zero exit this file is
  // supposed to produce either way -- the exit-status check would pass
  // either way. This fixture also crosses WorldBegin, so it hits the other
  // known leak on this path (GMANRenderManImpl::RiWorldBegin's output
  // driver, SPEC.md S8, phase-1 territory, untouched here): asserting no
  // LeakSanitizer output on this exact fixture would be a false claim.
  // display_badtype.rib and hider.rib below never reach WorldBegin, so they
  // carry that assertion instead.
  {
    const std::string path = ribDir + "/malformed/stringarray.rib";
    Result r = run(gman, path, /*debug=*/false);
    check(r.exitStatus != 0, "malformed: a non-string in a string array fails");
    check(r.output.find("Non-string in array") != std::string::npos,
	  "malformed: the diagnostic names the fault");
  }

  // Same fault class, a different leak shape, and (unlike stringarray.rib
  // above) never reaching WorldBegin -- so the "no LeakSanitizer output"
  // check here is clean of the other known leak and actually gates the
  // copyStringToken() fix. parseDisplay copies "name", then "type", then
  // "mode" before releasing any of them, and no parseParameterList is
  // involved: a bad token where "type" belongs throws out of
  // copyStringToken() itself with "name" already on the heap. The check is
  // a NOP on a build without -fsanitize=address (nothing ever prints that
  // banner, so it trivially holds) and a hard failure under the debug
  // preset or the CI sanitizer leg if this path leaks again.
  {
    const std::string path = ribDir + "/malformed/display_badtype.rib";
    Result r = run(gman, path, /*debug=*/false);
    check(r.exitStatus != 0, "malformed: a non-string Display type fails");
    check(r.output.find("Expecting string token") != std::string::npos,
	  "malformed: the diagnostic names the fault");
    check(r.output.find("LeakSanitizer") == std::string::npos,
	  "malformed: no LeakSanitizer report (display_badtype.rib)");
  }

  // Not malformed -- the point is the opposite. parseHider used to leak its
  // copied "type" string unconditionally, on every successful Hider request,
  // not only on an error path. This file is well-formed, never reaches
  // WorldBegin, and gman is expected to exit 0.
  {
    const std::string path = ribDir + "/hider.rib";
    Result r = run(gman, path, /*debug=*/false);
    check(r.exitStatus == 0, "hider: a well-formed Hider request parses");
    check(r.output.find("LeakSanitizer") == std::string::npos,
	  "hider: no LeakSanitizer report (hider.rib)");
  }

  return checkSummary("RIB dialect coverage holds");
}
