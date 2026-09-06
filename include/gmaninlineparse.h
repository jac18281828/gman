/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*--------------------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/07/21  First release
  --------------------------------------------------------------------
  A class to parse RiDeclare and inline parameter list definitions.
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

#ifndef __GMANINLINEPARSE_H
#define __GMANINLINEPARSE_H 1

#include <stdio.h>
#include <ctype.h>
#include <string>

#include "ri.h"
#include "gmanlog.h"
#include "gmandictionary.h"
#include "gmanerror.h"

class GMAN_EXPORT  GMANInlineParse
{
private:
  RtInt number_of_words;
  std::string word[7];

  bool inline_def;
  GMANTokenEntry::TokenClass tc;
  GMANTokenEntry::TokenType tt;
  RtInt size;
  std::string identifier;


  bool is_class (std::string str);
  bool is_type (std::string str);
  bool is_int (std::string str);

  GMANTokenEntry::TokenClass get_class (std::string str);
  GMANTokenEntry::TokenType get_type (std::string str);
  RtInt get_size (std::string str);

  RtVoid check_syntax ();
  RtVoid lc(std::string &);

public:
  RtVoid       parse (std::string str);

  bool       isInline() { return inline_def; }
  GMANTokenEntry::TokenClass getClass() { return tc; }
  GMANTokenEntry::TokenType  getType() { return tt; }
  RtInt      getQuantity() { return size; }
  std::string     getIdentifier() { return identifier; }
};

#endif

