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
  // this test. Revert step 1 (graceful unknown-request recovery) or step 7
  // (array parameters, the unbracketed "fov" form Projection here uses) and
  // this fails.
  {
    const std::string corpus = ribDir + "/corpus/menger.rib";
    Result r = run(gman, corpus, /*debug=*/false);
    check(r.exitStatus == 0, "corpus: menger.rib parses to completion, exit 0");
    check(r.output.find("ERROR") == std::string::npos,
	  "corpus: no error reported");
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

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  std::printf("RIB dialect coverage holds\n");
  return 0;
}
