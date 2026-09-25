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

#include "gmanparameterlist.h"

// Allocates one entry's array, fills it with copy, and appends it to block;
// copy takes the array to fill and applies whichever of copy_float/
// copy_integer/copy_string matches T.
template <typename T, typename CopyFn>
RtVoid GMANParameterList::appendEntry(std::vector<Entry>& block, GMANTokenId id, RtInt size, CopyFn copy) {
  auto data = std::make_unique<T[]>(size);
  copy(data.get());
  block.push_back(Entry{id, std::move(data)});
}

GMANParameterList::GMANParameterList(GMANDictionary& di, RtInt n, RtToken* tk, RtPointer* dt, RtInt vertex,
                                     RtInt varying, RtInt uniform, RtInt facevarying, const RtInt* suppliedCounts) {
  auto block = std::make_shared<std::vector<Entry>>();
  block->reserve(n);

  for (int i = 0; i < n; i++) { // convert token to id
    GMANTokenId tid;
    try {
      tid = di.getTokenId(std::string(tk[i]));
    } catch (GMANError& r) {
      GMANHandleError(r);
      continue;
    }

    const auto size = di.allocSize(tid, vertex, varying, uniform, facevarying);
    const auto supplied = suppliedCounts ? suppliedCounts[i] : size;
    if (supplied < size) {
      warning("Parameter \"{}\": declared length {}, supplied length {}; "
              "clamping and zero-filling the remainder.",
              tk[i], size, supplied);
    } else if (supplied > size) {
      warning("Parameter \"{}\": declared length {}, supplied length {}; "
              "truncating the excess.",
              tk[i], size, supplied);
    }
    switch (di.getType(tid)) {
    case GMANTokenEntry::STRING:
      appendEntry<std::string>(*block, tid, size,
                               [&](std::string* dest) { copy_string(size, (char**)dt[i], dest, supplied); });
      break;
    case GMANTokenEntry::INTEGER:
      appendEntry<RtInt>(*block, tid, size, [&](RtInt* dest) { copy_integer(size, (RtInt*)dt[i], dest, supplied); });
      break;
    default:
      appendEntry<RtFloat>(*block, tid, size,
                           [&](RtFloat* dest) { copy_float(size, (RtFloat*)dt[i], dest, supplied); });
      break;
    }
  }

  entries = std::move(block);
}

// Absence of a token is the routine case: every caller asks "did the user
// pass this optional parameter," not "is this parameter list well-formed."
// Every call site in the tree (RiWorldBegin's fov lookup, and the shader/
// light "tryGet" wrappers) already treats a missing token as NULL and either
// falls back to a spec default or wraps this call in try/catch to force
// that meaning by hand. Returning NULL directly makes that the only meaning
// there is to get.
RtPointer GMANParameterList::getPointer(GMANTokenId tid) const {
  if (!entries) {
    return NULL;
  }
  for (auto const& entry : *entries) {
    if (tid == entry.id) {
      return std::visit([](auto const& data) -> RtPointer { return data.get(); }, entry.data);
    }
  }
  return NULL;
}

RtVoid GMANParameterList::copy_float(RtInt n, RtFloat* source, RtFloat* dest, RtInt supplied) {
  int count = supplied < n ? supplied : n;
  int i;
  for (i = 0; i < count; i++)
    dest[i] = source[i];
  for (; i < n; i++)
    dest[i] = 0.0;
}
RtVoid GMANParameterList::copy_integer(RtInt n, RtInt* source, RtInt* dest, RtInt supplied) {
  int count = supplied < n ? supplied : n;
  int i;
  for (i = 0; i < count; i++)
    dest[i] = source[i];
  for (; i < n; i++)
    dest[i] = 0;
}
RtVoid GMANParameterList::copy_string(RtInt n, char** source, std::string* dest, RtInt supplied) {
  int count = supplied < n ? supplied : n;
  int i;
  for (i = 0; i < count; i++)
    dest[i] = std::string(source[i]);
  for (; i < n; i++)
    dest[i] = std::string();
}
