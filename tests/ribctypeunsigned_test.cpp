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
 * GMANRIBTokenize::consumeWhitespace, isKeyToken and isNumToken classify a
 * byte read off the input stream. char is signed on every platform this
 * tree builds for, so a byte >= 0x80 arrives as a negative value; these
 * checks pin the classification boundary those three call sites already
 * hold, and the string and comment paths that carry a high byte through
 * without classifying it at all.
 */

#include <sstream>
#include <string>

#include "check.h"
#include "gmanerror.h"
#include "gmanribtokenize.h"
#include "ri.h"

namespace {

// Runs getNext once more on an already-advanced stream and reports whether
// it threw a GMANError, and if so, its code and message.
struct ThrowResult {
  bool threw = false;
  RtInt code = 0;
  std::string message;
};

ThrowResult expectThrow(GMANRIBTokenize& tokenizer, std::istream& stream) {
  ThrowResult result;
  try {
    tokenizer.getNext(stream);
  } catch (GMANError const& error) {
    result.threw = true;
    result.code = error.getCode();
    result.message = error.getMessage();
  }
  return result;
}

} // namespace

int main() {
  // 1. A high byte ends a keyword like any other delimiter.
  {
    std::istringstream stream(std::string("matte") + '\xC3');
    GMANRIBTokenize tokenizer;
    const GMANToken token = tokenizer.getNext(stream);
    check(token.getType() == GMANToken::RI_MATTE, "keyword: high byte delimits 'matte' into RI_MATTE");

    const ThrowResult thrown = expectThrow(tokenizer, stream);
    check(thrown.threw, "keyword: the high byte itself throws on the next getNext");
    check(thrown.code == RIE_BADFILE, "keyword: the throw carries RIE_BADFILE");
  }

  // 2. A high byte ends a numeric literal like any other delimiter.
  {
    std::istringstream stream(std::string("12") + '\xC3');
    GMANRIBTokenize tokenizer;
    const GMANToken token = tokenizer.getNext(stream);
    check(token.getType() == GMANToken::LONGINT, "number: high byte delimits '12' into a LONGINT");
    check(token.getInt() == 12, "number: the LONGINT reads back as 12");

    const ThrowResult thrown = expectThrow(tokenizer, stream);
    check(thrown.threw, "number: the high byte itself throws on the next getNext");
    check(thrown.code == RIE_BADFILE, "number: the throw carries RIE_BADFILE");
  }

  // 3. A high byte standing alone is rejected the same way an unsupported
  // ASCII symbol is.
  {
    std::string input;
    input += ' ';
    input += '\xC3';
    std::istringstream stream(input);
    GMANRIBTokenize tokenizer;

    const ThrowResult thrown = expectThrow(tokenizer, stream);
    check(thrown.threw, "standalone: a lone high byte throws");
    check(thrown.code == RIE_BADFILE, "standalone: the throw carries RIE_BADFILE");
    check(thrown.message.find("Unknown character in ribfile") != std::string::npos,
          "standalone: the message names the unknown character");
  }

  // 4. A high byte inside a quoted string decodes untouched.
  {
    std::string input = "\"";
    input += '\xC3';
    input += '\xA9';
    input += '\"';
    std::istringstream stream(input);
    GMANRIBTokenize tokenizer;
    const std::string decoded = tokenizer.getNext(stream).getString();
    check(decoded.size() == 2 && static_cast<unsigned char>(decoded[0]) == 0xC3 &&
              static_cast<unsigned char>(decoded[1]) == 0xA9,
          "string: the two high bytes decode back untouched, in order");
  }

  // 5. A high byte inside a '#' comment does not disturb the next token.
  {
    std::string input = "#";
    input += '\xC3';
    input += " bad comment\n42";
    std::istringstream stream(input);
    GMANRIBTokenize tokenizer;
    const GMANToken token = tokenizer.getNext(stream);
    check(token.getType() == GMANToken::LONGINT, "comment: the token after a high byte in a comment is a LONGINT");
    check(token.getInt() == 42, "comment: that LONGINT reads back as 42");
  }

  return checkSummary("the rib tokenizer classifies a high byte the same way on both sides of the fix");
}
