/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * Defect 1 (uat-defects prompt): `Surface` before `AttributeBegin`/
 * `FrameBegin` segfaulted (exit 139) after writing a plausible-looking
 * TIFF -- a double free at process exit. Both requests push
 * GMANGraphicState::attributesStack.push(attributesStack.top()), which
 * copies GMANAttributes. GMANAttributes owned six GMANLoadableShader*
 * members (a dlopen'd module, closed with dlclose in ~GMANLoadable) with
 * no copy constructor or assignment operator, so the compiler-generated
 * ones did a shallow pointer copy: the pushed copy and the original both
 * held the same GMANLoadableShader*, and both destructors dlclose'd the
 * same handle.
 *
 * The push itself is correct RenderMan semantics -- an attribute block
 * inherits its parent's attributes -- so the fix is GMANAttributes getting
 * correct copy semantics, not avoiding the copy. The six members are now
 * std::shared_ptr<GMANLoadableShader>: a loaded module is immutable once
 * loaded, so sharing it rather than deep-copying also avoids re-dlopen'ing
 * on every AttributeBegin/AttributeEnd pair that does not change shaders.
 *
 * Not covered here: SolidBegin shares the same attributesStack shape but
 * cannot be reached from RIB (GMANRIBParse::parseSolidBegin never calls
 * RiSolidBeginV) -- a .rib fixture through it would pass on broken code.
 *
 * Revert check: reverting GMANAttributes back to raw GMANLoadableShader*
 * members (no explicit copy protection) reintroduces the double free --
 * both scenes below go back to a non-zero/signal exit status.
 */

#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
  if (!ok) {
    ++failures;
  }
}

int runGman(const std::string &gman, const std::string &rib) {
  const std::string command = "\"" + gman + "\" \"" + rib + "\" >/dev/null 2>&1";
  int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void writeFile(const std::string &path, const std::string &contents) {
  std::ofstream out(path);
  out << contents;
}

bool nonEmptyFile(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <gman-binary>\n", argv[0]);
    return 2;
  }
  const std::string gman = argv[1];

  // ---- Surface declared once, then used inside AttributeBegin/End ----
  const char *attrRib =
      "Display \"attr.tif\" \"file\" \"rgb\"\n"
      "Format 64 64 1\n"
      "Projection \"perspective\" \"fov\" [45]\n"
      "WorldBegin\n"
      "Surface \"matte\"\n"
      "AttributeBegin\n"
      "  Translate 0 0 5\n"
      "  Sphere 1 -1 1 360\n"
      "AttributeEnd\n"
      "WorldEnd\n";
  writeFile("attr.rib", attrRib);
  check(runGman(gman, "attr.rib") == 0,
        "Surface before AttributeBegin runs to completion");
  check(nonEmptyFile("attr.tif"), "AttributeBegin scene wrote a TIFF");

  // ---- Surface declared once, then a FrameBegin/FrameEnd pair (outside
  // WorldBegin, with a frame number -- nested inside WorldBegin fails on an
  // unrelated RIE_ILLSTATE and would be a false negative here) ----
  const char *frameRib =
      "Display \"frame.tif\" \"file\" \"rgb\"\n"
      "Format 64 64 1\n"
      "Projection \"perspective\" \"fov\" [45]\n"
      "Surface \"matte\"\n"
      "FrameBegin 1\n"
      "WorldBegin\n"
      "  Translate 0 0 5\n"
      "  Sphere 1 -1 1 360\n"
      "WorldEnd\n"
      "FrameEnd\n";
  writeFile("frame.rib", frameRib);
  check(runGman(gman, "frame.rib") == 0,
        "Surface before FrameBegin runs to completion");
  check(nonEmptyFile("frame.tif"), "FrameBegin scene wrote a TIFF");

  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("attributes copy holds\n");
  return 0;
}
