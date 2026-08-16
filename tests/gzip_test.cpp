/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Step 6: transparent gzip decompression, detected by magic bytes rather
 * than the ".rib" extension -- exporters produce both gzip'd-and-so-named
 * and gzip'd-but-named-".rib" files.
 */

#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

int runIn(const std::string &gman, const std::string &rib,
	 const std::string &workdir) {
  const std::string command = "cd \"" + workdir + "\" && \"" + gman +
    "\" \"" + rib + "\" > run.log 2>&1";
  const int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string slurp(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 4) {
    std::fprintf(stderr,
		 "usage: %s <gman-binary> <tests/rib/gzip dir> <scratch dir>\n",
		 argv[0]);
    return 2;
  }

  const std::string gman = argv[1];
  const std::string gzipDir = argv[2];
  const std::string scratch = argv[3];

  const std::string plainWork = scratch + "/plain";
  const std::string gzWork = scratch + "/compressed";
  const std::string misnamedWork = scratch + "/misnamed";
  /* Each source renders in its own directory so the three gzip_out.tif can be
   * compared. A directory that failed to appear would silently run gman
   * somewhere else, so the result is checked rather than discarded. */
  for (const std::string &dir : {plainWork, gzWork, misnamedWork}) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
      std::fprintf(stderr, "cannot create %s: %s\n", dir.c_str(),
		   ec.message().c_str());
      return 2;
    }
  }

  const int plainExit = runIn(gman, gzipDir + "/plain.rib", plainWork);
  const int gzExit = runIn(gman, gzipDir + "/plain_gz.rib.gz", gzWork);
  const int misnamedExit = runIn(gman, gzipDir + "/misnamed.rib", misnamedWork);

  check(plainExit == 0, "plain.rib parses");
  check(gzExit == 0, "plain_gz.rib.gz (gzip'd, gzip-named) parses");
  check(misnamedExit == 0,
	"misnamed.rib (gzip'd, .rib-named) parses -- magic bytes, not extension");

  const std::string plainImage = slurp(plainWork + "/gzip_out.tif");
  const std::string gzImage = slurp(gzWork + "/gzip_out.tif");
  const std::string misnamedImage = slurp(misnamedWork + "/gzip_out.tif");

  check(! plainImage.empty(), "plain.rib produced an image");
  check(plainImage == gzImage,
	"gzip'd and plain sources parse to byte-identical output");
  check(plainImage == misnamedImage,
	"a gzip'd file misnamed .rib parses identically (magic-byte detection)");

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  std::printf("gzip transparency holds\n");
  return 0;
}
