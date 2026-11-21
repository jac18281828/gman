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

#include <cstddef>
#include <format>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "gmandictionary.h"
#include "gmanerror.h"
#include "gmaninlineparse.h"
#include "gmanlog.h"
#include "gmanparameterlist.h"
#include "ri.h"

namespace {

std::string parameterName(std::string const& token) {
  gman::InlineParse parse;
  parse.parse(token);
  return parse.isInline() ? parse.getIdentifier() : token;
}

void checkSuppliedLength(std::string const& token, RtInt size, RtInt supplied, bool counted) {
  if (supplied < 0) {
    throw GMANError(RIE_RANGE, RIE_ERROR,
                    std::format("Parameter \"{}\": supplied length {} is negative", token, supplied).c_str());
  }
  if (counted && supplied < size) {
    throw GMANError(RIE_CONSISTENCY, RIE_ERROR,
                    std::format("Parameter \"{}\": declared length {}, supplied length {}; the array is shorter than "
                                "the request requires.",
                                token, size, supplied)
                        .c_str());
  }
  if (supplied > size) {
    warning("Parameter \"{}\": declared length {}, supplied length {}; "
            "truncating the excess.",
            token, size, supplied);
  }
}

} // namespace

// Allocates one entry's array, fills it with copy, and appends it to block;
// copy takes the array to fill and applies whichever of copy_float/
// copy_integer/copy_string matches T. A failed allocation throws RIE_NOMEM
// naming token and leaves block as it was.
template <typename T, typename CopyFn>
RtVoid GMANParameterList::appendEntry(std::vector<Entry>& block, GMANTokenId id, std::string name,
                                      std::string const& token, std::size_t size, CopyFn copy) {
  try {
    auto data = std::make_unique<T[]>(size);
    copy(data.get());
    block.push_back(Entry{id, std::move(name), static_cast<RtInt>(size), std::move(data)});
  } catch (std::bad_alloc const&) {
    throw GMANError(RIE_NOMEM, RIE_ERROR,
                    std::format("Parameter \"{}\": cannot allocate {} elements.", token, size).c_str());
  }
}

GMANParameterList::GMANParameterList(GMANDictionary& di, RtInt n, RtToken* tk, RtPointer* dt, RtInt vertex,
                                     RtInt varying, RtInt uniform, RtInt facevarying, const RtInt* suppliedCounts) {
  if (n < 0) {
    throw GMANError(RIE_RANGE, RIE_ERROR, std::format("GMANParameterList: parameter count {} is negative", n).c_str());
  }
  auto block = std::make_shared<std::vector<Entry>>();
  block->reserve(n);

  for (int i = 0; i < n; i++) { // convert token to id
    GMANTokenId tid;
    std::string const token(tk[i]);
    try {
      tid = di.getTokenId(token);
    } catch (GMANError& r) {
      GMANHandleError(r);
      continue;
    }

    const auto size = di.allocSize(tid, vertex, varying, uniform, facevarying);
    const auto supplied = suppliedCounts ? suppliedCounts[i] : size;
    checkSuppliedLength(token, size, supplied, suppliedCounts != nullptr);
    auto name = parameterName(token);
    switch (di.getType(tid)) {
    case GMANTokenEntry::STRING:
      appendEntry<std::string>(*block, tid, std::move(name), token, static_cast<std::size_t>(size),
                               [&](std::string* dest) { copy_string(size, (char**)dt[i], dest); });
      break;
    case GMANTokenEntry::INTEGER:
      appendEntry<RtInt>(*block, tid, std::move(name), token, static_cast<std::size_t>(size),
                         [&](RtInt* dest) { copy_integer(size, (RtInt*)dt[i], dest); });
      break;
    default:
      appendEntry<RtFloat>(*block, tid, std::move(name), token, static_cast<std::size_t>(size),
                           [&](RtFloat* dest) { copy_float(size, (RtFloat*)dt[i], dest); });
      break;
    }
  }

  entries = std::move(block);
}

// Absence of a token is the routine case: every caller asks "did the user
// pass this optional parameter," not "is this parameter list well-formed."
// Every call site in the tree (RiWorldBegin's fov lookup, and the light
// "tryGetPointer" wrapper) already treats a missing token as NULL and either
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

template <typename T> std::span<T const> GMANParameterList::valuesOf(std::string_view name) const {
  if (!entries) {
    return {};
  }
  for (auto const& entry : *entries) {
    if (entry.name != name) {
      continue;
    }
    if (auto const* data = std::get_if<std::unique_ptr<T[]>>(&entry.data)) {
      return {data->get(), static_cast<std::size_t>(entry.count)};
    }
  }
  return {};
}

std::span<RtFloat const> GMANParameterList::floats(std::string_view name) const { return valuesOf<RtFloat>(name); }

std::span<RtInt const> GMANParameterList::integers(std::string_view name) const { return valuesOf<RtInt>(name); }

std::span<std::string const> GMANParameterList::strings(std::string_view name) const {
  return valuesOf<std::string>(name);
}

RtVoid GMANParameterList::copy_float(RtInt n, RtFloat* source, RtFloat* dest) {
  for (RtInt i = 0; i < n; i++)
    dest[i] = source[i];
}
RtVoid GMANParameterList::copy_integer(RtInt n, RtInt* source, RtInt* dest) {
  for (RtInt i = 0; i < n; i++)
    dest[i] = source[i];
}
RtVoid GMANParameterList::copy_string(RtInt n, char** source, std::string* dest) {
  for (RtInt i = 0; i < n; i++)
    dest[i] = std::string(source[i]);
}
