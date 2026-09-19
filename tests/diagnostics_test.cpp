/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
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
 * libgman raises RIE_NOTSTARTED with the message "No active context" and
 * a malformed RiDeclare's RIE_SYNTAX with the message "BAD_SYNTAX",
 * through RiErrorHandler.
 */

#include <string>

#include "check.h"
#include "ri.h"

namespace {

RtInt lastCode = 0;
std::string lastMessage;

RtVoid recordingErrorHandler(RtInt code, RtInt, char const* message) {
  lastCode = code;
  lastMessage = message;
}

} // namespace

int main() {
  RiErrorHandler(recordingErrorHandler);

  // The active context is process-wide, so this runs before any RiBegin.
  RiWorldBegin();
  check(lastCode == RIE_NOTSTARTED, "a request before RiBegin is RIE_NOTSTARTED");
  check(lastMessage == "No active context", "the message is exactly \"No active context\"");

  RiBegin(RI_NULL);
  char name[] = "x";
  char declaration[] = "bogus";
  RiDeclare(name, declaration);
  check(lastCode == RIE_SYNTAX, "a malformed RiDeclare is RIE_SYNTAX");
  check(lastMessage == "BAD_SYNTAX", "the message is exactly \"BAD_SYNTAX\"");
  RiEnd();

  return checkSummary("diagnostics name the problem, not a retired class");
}
