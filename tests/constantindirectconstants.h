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

#pragma once

/*
 * constantindirect_test_plugin.cpp's own answer once prepare() has seen a
 * primitive, read here rather than duplicated so the module and every
 * test that checks its output -- indirectpass_test.cpp and
 * indirectrender_test.cpp -- agree on one value. 255 * kConstantIndirect
 * is exactly 13, an integer, so adding it to a narrowed byte never shifts
 * that byte's own fractional part.
 */
constexpr float kConstantIndirect = 13.0f / 255.0f;
