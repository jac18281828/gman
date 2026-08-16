/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Phase 2's RIB-dialect coverage: the corpus test, the per-request
 * parameter-consumption tests, and the unknown-request skip test.
 *
 * Each request test is deliberately "request under test, then a Sphere":
 * a handler that mis-counts its own arguments desyncs the token stream, and
 * that failure surfaces as the *following* request failing to parse, not the
 * request itself -- a request tested in isolation would miss it entirely.
 */

#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

struct Result {
  int exitStatus;
  std::string output;
};

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

  // Every RISpec 3.2 request phase 2 added parse-and-ignore coverage for.
  // ifelse.rib and solid.rib each cover a pair of requests that only mean
  // anything together (IfBegin/ElseIf/Else/IfEnd, SolidBegin/SolidEnd).
  const std::vector<std::string> requestFixtures = {
    "curves.rib", "blobby.rib", "subdivisionmesh.rib", "procedural.rib",
    "solid.rib", "detail.rib", "detailrange.rib", "relativedetail.rib",
    "skew.rib", "matte.rib", "trimcurve.rib", "errorhandler.rib",
    "archiverecord.rib", "maketexture.rib", "makebump.rib",
    "makelatlongenvironment.rib", "makecubefaceenvironment.rib",
    "makeshadow.rib", "ifelse.rib",
  };

  for (const std::string &fixture : requestFixtures) {
    const std::string path = ribDir + "/requests/" + fixture;
    Result r = run(gman, path, /*debug=*/true);
    check(r.exitStatus == 0, fixture + ": gman exits 0");
    check(r.output.find("Keyword token: Sphere") != std::string::npos,
	  fixture + ": the Sphere after it still parses (no desync)");
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
  {
    const std::string corpus = ribDir + "/corpus/menger.rib";
    Result r = run(gman, corpus, /*debug=*/false);
    check(r.exitStatus == 0, "corpus: menger.rib parses to completion, exit 0");
    check(r.output.find("ERROR") == std::string::npos,
	  "corpus: no error reported");
  }

  // ReadArchive of a gzip'd child -- steps 5 and 6 composed, which no other
  // fixture covers (readarchive_test.cpp uses plain files, gzip_test.cpp a
  // gzip'd top-level file).
  //
  // bike.rib reads bikeData.rib.gz. openRibStream decompresses the archive
  // whole before parsing begins, so reaching any token inside it exercises
  // all of step 6; TransformBegin is the archive's first request and appears
  // nowhere in bike.rib itself, which is what makes it evidence rather than
  // coincidence.
  //
  // The run stops shortly after, at Surface "plastic" -- a shader GMAN has
  // never shipped, a phase-3 gap unrelated to parsing (see tests/rib/README).
  // So this fixture covers decompression plus the archive's opening requests,
  // NOT its 5,216 Patch requests. Whoever lands the shader should revisit
  // this assertion; until then, claiming more would be claiming coverage that
  // does not exist.
  {
    const std::string bike = ribDir + "/corpus/bike.rib";
    Result r = run(gman, bike, /*debug=*/true);
    check(r.output.find("Keyword token: ReadArchive") != std::string::npos,
	  "corpus: bike.rib reaches its ReadArchive");
    check(r.output.find("Keyword token: TransformBegin") != std::string::npos,
	  "corpus: the gzip'd archive decompresses and its requests reach the "
	  "parser");
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

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  std::printf("RIB dialect coverage holds\n");
  return 0;
}
