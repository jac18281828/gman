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
 * A failed .rib load leaves no context current: RiBegin raises the load
 * error, every later request raises RIE_NOTSTARTED until RiBegin or
 * RiContext selects a context, and the outer context, opened first,
 * survives untouched for RiContext to return to.
 *
 * argv[1] is the built libgmanrib.so; this test renames it out of the way
 * for the duration of the run and restores it before exiting, pass or
 * fail, so a normal exit never leaves it renamed. tests/CMakeLists.txt
 * pairs this test with an idempotent FIXTURES_CLEANUP test that restores
 * the file too, for the crash-or-timeout case this program's own restore
 * can't reach.
 */

#include <stdio.h>

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

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <path-to-libgmanrib.so>\n", argv[0]);
    return 1;
  }
  const char* pluginPath = argv[1];
  char hiddenPath[4096];
  snprintf(hiddenPath, sizeof(hiddenPath), "%s.hidden-for-test", pluginPath);

  if (rename(pluginPath, hiddenPath) != 0) {
    perror("rename");
    return 1;
  }

  RiErrorHandler(countingErrorHandler);

  RiBegin(RI_NULL);
  RtContextHandle outer = RiGetContext();

  errorCount = 0;
  RiBegin("y.rib");
  check(errorCount == 1, "the load error is raised");
  check(lastCode == RIE_SYSTEM, "the load error is RIE_SYSTEM");
  check(RiGetContext() == RI_NULL, "RiGetContext returns no context");

  errorCount = 0;
  RiWorldBegin();
  check(errorCount == 1, "a request with no active context is rejected");
  check(lastCode == RIE_NOTSTARTED, "the rejection is RIE_NOTSTARTED");

  errorCount = 0;
  RiEnd();
  check(errorCount == 1, "RiEnd with no active context is rejected the same way");
  check(lastCode == RIE_NOTSTARTED, "so it cannot have ended the outer context");

  errorCount = 0;
  RiContext(outer);
  check(errorCount == 0, "RiContext returns to the outer context");
  check(RiGetContext() == outer, "which is current again");

  errorCount = 0;
  RiAttributeBegin();
  RiAttributeEnd();
  check(errorCount == 0, "the outer context still works");

  RiEnd();

  if (rename(hiddenPath, pluginPath) != 0) {
    perror("rename back");
    failures++;
  }

  if (failures != 0) {
    printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  printf("a failed .rib load leaves no context current\n");
  return 0;
}
