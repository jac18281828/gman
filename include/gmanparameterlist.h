/* SPDX-License-Identifier: LGPL-2.1-or-later */

/*---------------------------------------------------------
  Copyright (C) Lionel Joseph Lacour 2000, 2001
  2000/07/26  First release
  ---------------------------------------------------------
  A class to store parameter lists.
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

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "gman/export.h"
#include "gmandictionary.h"
#include "gmanerror.h"
#include "gmanlog.h"
#include "ri.h"

class GMAN_EXPORT GMANParameterList {
private:
  // Owns one parameter's array; the active alternative matches its type.
  struct Entry {
    GMANTokenId id;
    std::string name;
    RtInt count;
    std::variant<std::unique_ptr<RtFloat[]>, std::unique_ptr<RtInt[]>, std::unique_ptr<std::string[]>> data;
  };

  // Shared by every copy of this list; the last one to go frees the block.
  std::shared_ptr<std::vector<Entry>> entries;

  RtVoid copy_float(RtInt number, RtFloat* source, RtFloat* dest);
  RtVoid copy_integer(RtInt number, RtInt* source, RtInt* dest);
  RtVoid copy_string(RtInt number, char** source, std::string* dest);

  template <typename T, typename CopyFn>
  static RtVoid appendEntry(std::vector<Entry>& block, GMANTokenId id, std::string name, std::string const& token,
                            std::size_t size, CopyFn copy);

  template <typename T> std::span<T const> valuesOf(std::string_view name) const;

public:
  GMANParameterList() = default;

  // suppliedCounts, when present, is index-aligned with tk/dt: element i is
  // the length the caller actually supplied for dt[i], as opposed to
  // vertex/varying/uniform/facevarying, which say how long it is supposed
  // to be. A supplied length below the declared one stops the request with
  // RIE_CONSISTENCY, naming the parameter and both lengths, before that
  // parameter allocates; a longer one keeps the first declared-length
  // values and warns that it truncates the excess. NULL (the default) means
  // the caller's arrays are trusted at their declared length: the
  // RIB-parsed path, which does not control what a scene file supplies,
  // passes real counts; a program calling the public RI API directly is
  // trusted with its own memory, and the RISpec gives no way to describe
  // an RtPointer's length regardless.
  //
  // Throws GMANError at RIE_ERROR: RIE_CONSISTENCY for a supplied length
  // below the declared one; RIE_RANGE for a negative n, a negative supplied
  // length, or a parameter whose element count GMANDictionary::allocSize
  // rejects; RIE_NOMEM, naming the token, when an entry's allocation fails.
  // An entry already built when one of them is thrown is released.
  GMANParameterList(GMANDictionary& di, RtInt n, RtToken* tk, RtPointer* dt, RtInt vertex = 1, RtInt varying = 1,
                    RtInt uniform = 1, RtInt facevarying = 1, const RtInt* suppliedCounts = NULL);

  // Rule of zero: none of copy, move, or destroy is declared, so a moved-
  // from list's shared_ptr is left null instead of still owning its data.

  RtPointer getPointer(GMANTokenId tid) const;

  // The values of the first parameter named name whose stored type matches,
  // or an empty span when none does. A parameter's name is its token with any
  // inline declaration removed: "float ior" answers to "ior". The span is as
  // long as the parameter's allocated element count and stays valid while
  // this list or any copy of it lives.
  std::span<RtFloat const> floats(std::string_view name) const;
  std::span<RtInt const> integers(std::string_view name) const;
  std::span<std::string const> strings(std::string_view name) const;
};
