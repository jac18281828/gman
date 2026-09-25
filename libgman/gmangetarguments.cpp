/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* LJL
 * complete rewrite from John code.
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

#include "gmangetarguments.h"

namespace gman {

Arguments getArguments(va_list args) {
  Arguments a;
  RtToken t = va_arg(args, RtToken);
  while (t != RI_NULL) {
    a.tokens.push_back(t);
    a.parms.push_back(va_arg(args, RtPointer));
    t = va_arg(args, RtToken);
  }
  a.n = static_cast<RtInt>(a.tokens.size());
  return a;
}

} // namespace gman
