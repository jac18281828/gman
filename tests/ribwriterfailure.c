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
 * A failed .rib load must not leave the previous context current. argv[1]
 * is the built libgmanrib.so; this test renames it out of the way for the
 * duration of the run (RESOURCE_LOCK in tests/CMakeLists.txt keeps this
 * from racing gman_ribwriter, the only other user of that file) and
 * restores it before exiting, pass or fail.
 *
 * With the plugin unreachable, RiBegin("y.rib") must still push and
 * activate a new context -- exactly as a bad renderer name already does --
 * so the caller's RiGetContext and RiEnd operate on it, and the outer
 * context, opened first, survives untouched.
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
  check(errorCount == 1, "the failed load reports exactly one error");
  check(lastCode == RIE_SYSTEM, "the error is RIE_SYSTEM");
  check(RiGetContext() != outer, "a new context is current, not the outer one");

  RiEnd();
  check(RiGetContext() == RI_NULL, "ending the broken context clears current");

  errorCount = 0;
  RiContext(outer);
  check(errorCount == 0, "the outer context survives and is still reachable");

  RiEnd();

  if (rename(hiddenPath, pluginPath) != 0) {
    perror("rename back");
    failures++;
  }

  if (failures != 0) {
    printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  printf("a failed .rib load does not leak the outer context\n");
  return 0;
}
