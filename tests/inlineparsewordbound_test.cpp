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
 * gman::InlineParse::parse fills a seven-element word array through a loop
 * guarded j < 7, except the '['/']' branch, which can write it twice in one
 * iteration. A seven-word declaration followed directly by a bracket -- "a b
 * c d e f g[h]" -- closes word[6] on the first write, then writes word[7] on
 * the second, past the array's end: RIE_SYNTAX never fires because the
 * corrupted number_of_words (8) matches no case in check_syntax's switch.
 *
 * Revert check: reverting the bound on the bracket branch's second write
 * (libgman/gmaninlineparse.cpp) makes testTriggerThrowsSyntaxError go red --
 * plain ctest, no sanitizer required, since it asserts the throw check_syntax
 * itself skips once the array is corrupted.
 */

#include "check.h"
#include "gmandictionary.h"
#include "gmaninlineparse.h"

namespace {

void testTriggerThrowsSyntaxError() {
  gman::InlineParse ip;
  RtInt code = 0;
  bool threw = false;
  try {
    ip.parse("a b c d e f g[h]");
  } catch (GMANError& e) {
    threw = true;
    code = e.getCode();
  }
  check(threw, "a seven-word declaration followed by a bracket throws");
  check(code == RIE_SYNTAX, "the thrown error is RIE_SYNTAX");
}

void testSevenWordsNoBracketStillThrows() {
  gman::InlineParse ip;
  RtInt code = 0;
  bool threw = false;
  try {
    ip.parse("a b c d e f g");
  } catch (GMANError& e) {
    threw = true;
    code = e.getCode();
  }
  check(threw, "seven words with no trailing bracket still throw (case 7, unmoved)");
  check(code == RIE_SYNTAX, "the thrown error is still RIE_SYNTAX");
}

void testTwoWordForm() {
  gman::InlineParse ip;
  ip.parse("float x");
  check(ip.isInline(), "\"float x\" is an inline declaration");
  check(ip.getClass() == GMANTokenEntry::UNIFORM, "\"float x\" defaults to uniform class");
  check(ip.getType() == GMANTokenEntry::FLOAT, "\"float x\" parses type float");
  check(ip.getQuantity() == 1, "\"float x\" has quantity 1");
  check(ip.getIdentifier() == "x", "\"float x\" identifier is \"x\"");
}

void testThreeWordForm() {
  gman::InlineParse ip;
  ip.parse("uniform float x");
  check(ip.isInline(), "\"uniform float x\" is an inline declaration");
  check(ip.getClass() == GMANTokenEntry::UNIFORM, "\"uniform float x\" class is uniform");
  check(ip.getType() == GMANTokenEntry::FLOAT, "\"uniform float x\" type is float");
  check(ip.getQuantity() == 1, "\"uniform float x\" has quantity 1");
  check(ip.getIdentifier() == "x", "\"uniform float x\" identifier is \"x\"");
}

void testFiveWordForm() {
  gman::InlineParse ip;
  ip.parse("float [3] x");
  check(ip.isInline(), "\"float [3] x\" is an inline declaration");
  check(ip.getClass() == GMANTokenEntry::UNIFORM, "\"float [3] x\" defaults to uniform class");
  check(ip.getType() == GMANTokenEntry::FLOAT, "\"float [3] x\" type is float");
  check(ip.getQuantity() == 3, "\"float [3] x\" quantity is the bracketed 3");
  check(ip.getIdentifier() == "x", "\"float [3] x\" identifier is \"x\"");
}

void testSixWordForm() {
  gman::InlineParse ip;
  ip.parse("varying point [2] P");
  check(ip.isInline(), "\"varying point [2] P\" is an inline declaration");
  check(ip.getClass() == GMANTokenEntry::VARYING, "\"varying point [2] P\" class is varying");
  check(ip.getType() == GMANTokenEntry::POINT, "\"varying point [2] P\" type is point");
  check(ip.getQuantity() == 2, "\"varying point [2] P\" quantity is the bracketed 2");
  check(ip.getIdentifier() == "P", "\"varying point [2] P\" identifier is \"P\"");
}

} // namespace

int main() {
  testTriggerThrowsSyntaxError();
  testSevenWordsNoBracketStillThrows();
  testTwoWordForm();
  testThreeWordForm();
  testFiveWordForm();
  testSixWordForm();

  return checkSummary("InlineParse's word array stays bounded");
}
