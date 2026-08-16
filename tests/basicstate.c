/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (c) 2026 John Cairns <john@2ad.com>
 */

/*
 * RI state machine, exercised through the published C API.
 *
 * The file this replaces could never have worked: it drew via
 * RiObjectBegin/RiObjectInstance, which are no-op stubs, called
 * RiBegin("libgmanraytracer.so") -- which GMANRenderMan turns into
 * dlopen("liblibgmanraytracer.so.so") -- and issued RiSphere before
 * RiWorldBegin, which GMANGraphicState rejects.
 *
 * What is tested here is the part that does work: GMANGraphicState's table of
 * which RI requests are legal in which begin/end block. Errors are routed to
 * a handler that counts them rather than printing, so both the accept and the
 * reject cases are assertable. Nothing here renders, so this test says
 * nothing about output and no phase supersedes it.
 */

#include <stdio.h>

#include "ri.h"

static int errorCount = 0;
static RtInt lastCode = 0;
static RtInt lastSeverity = 0;

static RtVoid countingErrorHandler(RtInt code, RtInt severity, const char *msg)
{
  errorCount++;
  lastCode = code;
  lastSeverity = severity;
  printf("    handler: code=%d severity=%d %s\n", (int) code, (int) severity, msg);
}

static int failures = 0;

static void check(int ok, const char *what)
{
  printf("%s: %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) {
    failures++;
  }
}

int main(void)
{
  RiErrorHandler(countingErrorHandler);

  /* The default renderer name is gmanzbuffer; RiBegin dlopens it. */
  RiBegin(RI_NULL);
  check(errorCount == 0, "RiBegin loads the default renderer without error");

  /* RiAttributeBegin is legal in the outermost block. */
  errorCount = 0;
  RiAttributeBegin();
  RiAttributeEnd();
  check(errorCount == 0, "RiAttributeBegin/End nest legally outside a world");

  /* A primitive outside a world block is not. */
  errorCount = 0;
  RiSphere(1.0, -1.0, 1.0, 360.0, RI_NULL);
  check(errorCount == 1, "RiSphere before RiWorldBegin is rejected");
  check(lastCode == RIE_ILLSTATE, "the rejection is RIE_ILLSTATE");

  /* Leaving a block that was never entered is not either. */
  errorCount = 0;
  RiAttributeEnd();
  check(errorCount == 1, "unmatched RiAttributeEnd is rejected");

  /* Phase 1: cmdTransformPoints' legal-block mask was written
   * B|F|W|A|T||S -- the doubled "||" collapses the whole expression to 1
   * (B alone), so RiTransformPoints was rejected everywhere except the
   * outermost block, including here, inside RiFrameBegin/End (F is one
   * of the masked-out blocks). */
  errorCount = 0;
  RiFrameBegin(1);
  RiTransformPoints("world", "object", 0, NULL);
  check(errorCount == 0, "RiTransformPoints is legal inside RiFrameBegin/End");
  RiFrameEnd();

  RiEnd();

  if (failures != 0) {
    printf("%d assertion(s) failed\n", failures);
    return 1;
  }

  printf("state machine holds\n");
  return 0;
}
