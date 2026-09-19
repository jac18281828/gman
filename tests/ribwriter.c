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
 * RiBegin("x.rib") routes to the RIB-writer plugin, through the published C
 * API, argv[1] is the output path (a .rib name under the build tree),
 * argv[2] the golden path.
 *
 * The scene sticks to requests the GMANASCII audit does not name as broken,
 * so it says nothing about G2's work. Left out, each for a listed finding:
 * LightSource and AreaLightSource (B1, no shader name or sequence number),
 * Patch/PatchMesh/Curves/NuPatch/TrimCurve (B2, F1), Declare (B3), filter
 * names on PixelFilter/Make* (B5), Procedural (B6), ObjectBegin/Instance
 * (B7), Hyperboloid (B8), Blobby/SubdivisionMesh (B9), ColorSamples (H5) and
 * any facevarying parameter (H6). Every argument keeps to six significant
 * digits so audit H1's precision loss cannot show.
 *
 * A second, unrelated context checks that RI_NULL still routes to the
 * renderer: an RiSphere before RiWorldBegin there raises RIE_ILLSTATE, which
 * GMANASCII never does.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ri.h"

static int errorCount = 0;
static RtInt lastCode = 0;

static RtVoid countingErrorHandler(RtInt code, RtInt severity, const char* msg) {
  errorCount++;
  lastCode = code;
  (void)severity;
  printf("    handler: code=%d severity=%d %s\n", (int)code, (int)severity, msg);
}

static int failures = 0;

static void check(int ok, const char* what) {
  printf("%s: %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) {
    failures++;
  }
}

/* Reads a whole file into a malloc'd buffer; *len receives its size. Returns
 * NULL if the file cannot be opened. */
static char* readWholeFile(const char* path, long* len) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  *len = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buf = malloc((size_t)*len);
  if (buf != NULL && *len > 0) {
    if (fread(buf, 1, (size_t)*len, f) != (size_t)*len) {
      free(buf);
      buf = NULL;
    }
  }
  fclose(f);
  return buf;
}

static void writeScene(void) {
  RtToken projTokens[] = {"fov"};
  RtFloat fov = 30;
  RtPointer projParms[] = {&fov};

  RtColor red = {1, 0, 0};
  RtColor white = {1, 1, 1};

  RtToken surfaceTokens[] = {"Ka", "Kd"};
  RtFloat ka = 1, kd = 0.5;
  RtPointer surfaceParms[] = {&ka, &kd};

  RtToken polyTokens[] = {"P"};
  RtFloat squareP[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  RtPointer polyParms[] = {squareP};

  RiFrameBegin(1);
  RiFormat(640, 480, 1);
  RiProjectionV("perspective", 1, projTokens, projParms);
  RiClipping(0.5, 50);
  RiPixelSamples(2, 2);
  RiScreenWindow(-1, 1, -1, 1);
  RiWorldBegin();
  RiAttributeBegin();
  RiTransformBegin();
  RiTranslate(0, 0, 5);
  RiRotate(45, 0, 1, 0);
  RiScale(1, 1, 1);
  RiColor(red);
  RiOpacity(white);
  RiSurfaceV("plastic", 2, surfaceTokens, surfaceParms);
  RiSphere(1, -1, 1, 360, RI_NULL);
  RiCone(1, 1, 360, RI_NULL);
  RiCylinder(1, -1, 1, 360, RI_NULL);
  RiDisk(1, 1, 360, RI_NULL);
  RiParaboloid(1, 0, 1, 360, RI_NULL);
  RiTorus(1, 0.5, 0, 360, 360, RI_NULL);
  RiPolygonV(4, 1, polyTokens, polyParms);
  RiTransformEnd();
  RiAttributeEnd();
  RiWorldEnd();
  RiFrameEnd();
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <output.rib> <golden.rib>\n", argv[0]);
    return 1;
  }
  const char* outputPath = argv[1];
  const char* goldenPath = argv[2];

  RiErrorHandler(countingErrorHandler);

  errorCount = 0;
  RiBegin(outputPath);
  writeScene();
  RiEnd();
  check(errorCount == 0, "the writer context raises no error");

  long outputLen = 0;
  long goldenLen = 0;
  char* outputBytes = readWholeFile(outputPath, &outputLen);
  char* goldenBytes = readWholeFile(goldenPath, &goldenLen);
  check(outputBytes != NULL, "the writer produced its output file");
  check(goldenBytes != NULL, "the golden file is readable");
  if (outputBytes != NULL && goldenBytes != NULL) {
    int matches = outputLen == goldenLen && memcmp(outputBytes, goldenBytes, (size_t)outputLen) == 0;
    check(matches, "the output matches the golden file byte for byte");
    if (!matches) {
      printf("---output (%ld bytes)---\n%.*s\n", outputLen, (int)outputLen, outputBytes);
      printf("---golden (%ld bytes)---\n%.*s\n", goldenLen, (int)goldenLen, goldenBytes);
    }
  }
  free(outputBytes);
  free(goldenBytes);

  /* The default route is untouched: RI_NULL still loads the renderer, whose
   * state machine (not the writer's) rejects a primitive outside a world
   * block. */
  errorCount = 0;
  RiBegin(RI_NULL);
  RiSphere(1, -1, 1, 360, RI_NULL);
  check(errorCount == 1, "RiSphere before RiWorldBegin is rejected in the renderer context");
  check(lastCode == RIE_ILLSTATE, "the rejection is RIE_ILLSTATE");
  RiEnd();

  if (failures != 0) {
    printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  printf("RiBegin routes a .rib name to the writer\n");
  return 0;
}
