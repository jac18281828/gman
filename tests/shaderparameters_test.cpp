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
 * A shader reads a parameter by name from a list built on a dictionary of
 * its own: declared with the dictionary or inline in the token, a float, a
 * color, a string or an integer array, each answering only to its own type
 * and length. Every list here is built on a fresh GMANDictionary, never the
 * standard one.
 */

#include <string>

#include "check.h"
#include "gmandictionary.h"
#include "gmanparameterlist.h"
#include "gmanshaderparams.h"

namespace {

constexpr RtFloat kDefaultFloat = -1.0f;
constexpr char const* kDefaultString = "default";

GMANColor const kDefaultColor(0.9f, 0.8f, 0.7f);

bool sameColor(GMANColor const& a, GMANColor const& b) {
  return a.getRed() == b.getRed() && a.getGreen() == b.getGreen() && a.getBlue() == b.getBlue();
}

void checkDeclaredAndInlineFloats() {
  GMANDictionary dictionary;
  dictionary.addToken("ior", GMANTokenEntry::UNIFORM, GMANTokenEntry::FLOAT);

  RtFloat ior = 1.33f;
  RtFloat ior2 = 1.7f;
  RtToken tokens[2] = {"ior", "float ior2"};
  RtPointer parms[2] = {&ior, &ior2};
  GMANParameterList const pl(dictionary, 2, tokens, parms);

  check(gmanshader::getFloatParam(pl, "ior", kDefaultFloat) == 1.33f,
        "a parameter declared with the dictionary reads by name");
  check(gmanshader::getFloatParam(pl, "ior2", kDefaultFloat) == 1.7f, "an inline parameter reads by its identifier");
  check(gmanshader::getFloatParam(pl, "float ior2", kDefaultFloat) == kDefaultFloat,
        "the inline declaration is no part of the name");
}

void checkColor() {
  GMANDictionary dictionary;

  RtColor tint = {0.1f, 0.2f, 0.3f};
  RtFloat one = 4.0f;
  RtToken tokens[2] = {"color tint", "float one"};
  RtPointer parms[2] = {tint, &one};
  GMANParameterList const pl(dictionary, 2, tokens, parms);

  check(sameColor(gmanshader::getColorParam(pl, "tint", kDefaultColor), GMANColor(0.1f, 0.2f, 0.3f)),
        "an inline color reads its three channels");
  check(sameColor(gmanshader::getColorParam(pl, "one", kDefaultColor), kDefaultColor),
        "a one-value parameter is too short for a color");
}

void checkStringAndTypeCheck() {
  GMANDictionary dictionary;

  char const* mapname[1] = {"wood.tx"};
  RtToken tokens[1] = {"string mapname"};
  RtPointer parms[1] = {mapname};
  GMANParameterList const pl(dictionary, 1, tokens, parms);

  check(gmanshader::getStringParam(pl, "mapname", kDefaultString) == "wood.tx", "an inline string reads by name");
  check(gmanshader::getFloatParam(pl, "mapname", kDefaultFloat) == kDefaultFloat,
        "a string parameter read as a float answers the default");
  check(pl.floats("mapname").empty(), "floats of a string parameter is empty");
}

void checkStandardToken() {
  GMANDictionary dictionary;

  RtFloat kd = 0.7f;
  RtToken tokens[1] = {RI_KD};
  RtPointer parms[1] = {&kd};
  GMANParameterList const pl(dictionary, 1, tokens, parms);

  check(gmanshader::getFloatParam(pl, RI_KD, kDefaultFloat) == 0.7f, "a standard token still reads by its name");
}

void checkAbsentName() {
  GMANDictionary dictionary;

  RtFloat kd = 0.7f;
  RtToken tokens[1] = {RI_KD};
  RtPointer parms[1] = {&kd};
  GMANParameterList const pl(dictionary, 1, tokens, parms);
  GMANParameterList const empty;

  check(gmanshader::getFloatParam(pl, "absent", kDefaultFloat) == kDefaultFloat,
        "an absent name answers a float default");
  check(sameColor(gmanshader::getColorParam(pl, "absent", kDefaultColor), kDefaultColor),
        "an absent name answers a color default");
  check(gmanshader::getStringParam(pl, "absent", kDefaultString) == kDefaultString,
        "an absent name answers a string default");
  check(pl.floats("absent").empty() && pl.integers("absent").empty() && pl.strings("absent").empty(),
        "an absent name answers an empty span from every accessor");
  check(empty.floats("Kd").empty() && empty.integers("Kd").empty() && empty.strings("Kd").empty(),
        "a default list answers an empty span from every accessor");
}

void checkIntegerArray() {
  GMANDictionary dictionary;

  RtInt n[3] = {4, 5, 6};
  RtToken tokens[1] = {"integer[3] n"};
  RtPointer parms[1] = {n};
  GMANParameterList const pl(dictionary, 1, tokens, parms);

  auto const values = pl.integers("n");
  check(values.size() == 3, "an integer array reads back at its declared length");
  check(values.size() == 3 && values[0] == 4 && values[1] == 5 && values[2] == 6,
        "an integer array reads back its values");
  check(pl.floats("n").empty(), "floats of an integer parameter is empty");
}

} // namespace

int main() {
  checkDeclaredAndInlineFloats();
  checkColor();
  checkStringAndTypeCheck();
  checkStandardToken();
  checkAbsentName();
  checkIntegerArray();

  return checkSummary("shaders read declared and inline parameters by name");
}
