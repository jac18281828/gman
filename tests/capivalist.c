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
 * Drives RiSurface, RiOption, RiPolygon, RiSphere and RiMakeShadow through
 * their ellipsis form, not *V, so each reads its own va_list. RiBegin routes
 * to the RIB-writer plugin exactly as tests/ribwriter.c does; the written
 * text is byte-comparable to a golden file, so every token and value that
 * survives the trip is provably the one the caller supplied. RiCone carries
 * zero optional pairs, proving the fix leaves that path undisturbed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ri.h"

static int errorCount = 0;

static RtVoid countingErrorHandler(RtInt code, RtInt severity, const char* msg) {
  errorCount++;
  (void)code;
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
  RtInt bucketsize = 4;
  RtFloat ka = 1, kd = 0.5;
  RtFloat density = 2;
  RtFloat squareP[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  RtFloat bias = 0.25;

  RiFrameBegin(1);
  RiOption("limits", "constant integer bucketsize", &bucketsize, RI_NULL);
  RiWorldBegin();
  RiSurface("plastic", "Ka", &ka, "Kd", &kd, RI_NULL);
  RiSphere(1, -1, 1, 360, "constant float density", &density, RI_NULL);
  RiCone(1, 1, 360, RI_NULL);
  RiPolygon(4, "P", squareP, RI_NULL);
  RiMakeShadow("shadow.tif", "shadow.shd", "constant float bias", &bias, RI_NULL);
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

  if (failures != 0) {
    printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  printf("the ellipsis Ri* entry points carry their tokens and values through the RIB writer\n");
  return 0;
}
