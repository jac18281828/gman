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
 * Displacement, Atmosphere, Interior, Exterior and Imager each resolve
 * their token to lib<name>.so, exactly as Surface already does, and fall
 * back to a clean, unset state on any failure. Drives GMANAttributes and
 * GMANOptions directly, with a recording RiErrorHandler installed once.
 */

#include <functional>
#include <memory>
#include <string>

#include <dlfcn.h>

#include "check.h"
#include "gmanattributes.h"
#include "gmanoptions.h"
#include "gmanparameterlist.h"
#include "ri.h"

namespace {

RtInt lastCode = 0;
RtInt lastSeverity = 0;
std::string lastMessage;

RtVoid recordingErrorHandler(RtInt code, RtInt severity, char const* message) {
  lastCode = code;
  lastSeverity = severity;
  lastMessage = message;
}

void resetRecorded() {
  lastCode = RIE_NOERROR;
  lastSeverity = RIE_INFO;
  lastMessage.clear();
}

// countingimager is a plugin, dlopened at runtime by GMANOptions::
// setImager, never linked into this binary, so its own live-count function
// is resolved the same way: dlopen (idempotent) then dlsym, once, memoized
// here.
int countingImagerLiveCount() {
  using LiveCountFn = int (*)();
  static LiveCountFn const fn = []() -> LiveCountFn {
    void* const handle = dlopen("libcountingimager.so", RTLD_LAZY);
    if (handle == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<LiveCountFn>(dlsym(handle, "GMANCountingImagerLiveCount"));
  }();
  return fn ? fn() : -1;
}

// ---- Surface: today's setSurface already maps every name uniformly and
// quotes it verbatim; nothing here changes that. ----

void checkSurfaceUnchanged() {
  GMANParameterList const emptyPl;

  {
    GMANAttributes attr;
    resetRecorded();
    attr.setSurface("nosuchshader", emptyPl);
    check(lastMessage.rfind("Surface \"nosuchshader\" failed to load; the default surface shades instead: ", 0) == 0,
          "Surface \"nosuchshader\": the message starts with the expected prefix");
    check(attr.getSurface(0.0) == nullptr, "Surface \"nosuchshader\": getSurface is null");
  }
  {
    GMANAttributes attr;
    resetRecorded();
    attr.setSurface("gmanzbuffer", emptyPl);
    check(lastMessage == "Surface \"gmanzbuffer\" failed to load; the default surface shades instead: "
                         "Loadable module missing shader.",
          "Surface \"gmanzbuffer\": the message matches exactly");
    check(attr.getSurface(0.0) == nullptr, "Surface \"gmanzbuffer\": getSurface is null");
  }
  {
    GMANAttributes attr;
    resetRecorded();
    attr.setSurface("notasurface", emptyPl);
    check(lastMessage == "Surface \"notasurface\" failed to load; the default surface shades instead: "
                         "Specified surface shader is not a surface shader.",
          "Surface \"notasurface\": the message matches exactly");
    check(attr.getSurface(0.0) == nullptr, "Surface \"notasurface\": getSurface is null");
  }
  {
    GMANAttributes attr;
    resetRecorded();
    attr.setSurface("plastic.so", emptyPl);
    check(lastMessage.rfind("Surface \"plastic.so\" failed to load; the default surface shades instead: ", 0) == 0,
          "Surface \"plastic.so\": the message starts with the expected prefix");
    check(lastMessage.find("\"plastic\"") == std::string::npos,
          "Surface \"plastic.so\": the message never quotes the stripped name");
    check(lastMessage.find("did you mean") == std::string::npos,
          "Surface \"plastic.so\": the message suggests nothing");
    check(attr.getSurface(0.0) == nullptr, "Surface \"plastic.so\": getSurface is null");
  }
}

// ---- Displacement, Atmosphere, Interior, Exterior, Imager: seven checks
// each, against a fresh GMANAttributes or GMANOptions. ----

struct KindFixture {
  std::string requestName;
  std::string successName;
  std::function<void(std::string const&)> setter;
  std::function<bool()> hasValue;
  std::function<int()> liveCount; // empty for a kind with no live-count probe
};

KindFixture displacementFixture() {
  auto attr = std::make_shared<GMANAttributes>();
  KindFixture kind;
  kind.requestName = "Displacement";
  kind.successName = "identitydisplacement";
  kind.setter = [attr](std::string const& name) { attr->setDisplacement(name, GMANParameterList()); };
  kind.hasValue = [attr]() { return attr->getDisplacement(0.0) != nullptr; };
  return kind;
}

KindFixture atmosphereFixture() {
  auto attr = std::make_shared<GMANAttributes>();
  KindFixture kind;
  kind.requestName = "Atmosphere";
  kind.successName = "notasurface";
  kind.setter = [attr](std::string const& name) { attr->setAtmosphere(name, GMANParameterList()); };
  kind.hasValue = [attr]() { return attr->getAtmosphere(0.0) != nullptr; };
  return kind;
}

KindFixture interiorFixture() {
  auto attr = std::make_shared<GMANAttributes>();
  KindFixture kind;
  kind.requestName = "Interior";
  kind.successName = "notasurface";
  kind.setter = [attr](std::string const& name) { attr->setInterior(name, GMANParameterList()); };
  kind.hasValue = [attr]() { return attr->getInterior(0.0) != nullptr; };
  return kind;
}

KindFixture exteriorFixture() {
  auto attr = std::make_shared<GMANAttributes>();
  KindFixture kind;
  kind.requestName = "Exterior";
  kind.successName = "notasurface";
  kind.setter = [attr](std::string const& name) { attr->setExterior(name, GMANParameterList()); };
  kind.hasValue = [attr]() { return attr->getExterior(0.0) != nullptr; };
  return kind;
}

KindFixture imagerFixture() {
  auto opt = std::make_shared<GMANOptions>();
  KindFixture kind;
  kind.requestName = "Imager";
  kind.successName = "countingimager";
  kind.setter = [opt](std::string const& name) { opt->setImager(name, GMANParameterList()); };
  kind.hasValue = [opt]() { return opt->getImager() != nullptr; };
  kind.liveCount = countingImagerLiveCount;
  return kind;
}

// A setter never lets a GMANError escape: a pinned check that threw would
// exit through an uncaught exception rather than main's return.
void checkFailure(KindFixture const& kind, std::string const& name, std::string const& tag,
                  std::string const& mustContain = "") {
  resetRecorded();
  bool threw = false;
  try {
    kind.setter(name);
  } catch (...) {
    threw = true;
  }
  check(!threw, kind.requestName + " " + tag + ": the setter throws nothing");
  check(lastCode == RIE_NOSHADER, kind.requestName + " " + tag + ": the code is RIE_NOSHADER");
  check(lastSeverity == RIE_ERROR, kind.requestName + " " + tag + ": the severity is RIE_ERROR");
  check(lastMessage.find(kind.requestName) != std::string::npos,
        kind.requestName + " " + tag + ": the message names its own kind");
  check(lastMessage.find("\"" + name + "\"") != std::string::npos,
        kind.requestName + " " + tag + ": the message quotes \"" + name + "\"");
  if (!mustContain.empty()) {
    check(lastMessage.find(mustContain) != std::string::npos,
          kind.requestName + " " + tag + ": the message names " + mustContain);
  }
  check(!kind.hasValue(), kind.requestName + " " + tag + ": the getter reports nothing");
}

void checkDotSoNotSniffed(KindFixture const& kind) {
  std::string const doubled = kind.successName + ".so";
  checkFailure(kind, doubled, "a name already ending in .so");
  check(lastMessage.find("did you mean") == std::string::npos,
        kind.requestName + " \"" + doubled + "\": the message suggests nothing");
  check(lastMessage.find("\"" + kind.successName + "\"") == std::string::npos,
        kind.requestName + " \"" + doubled + "\": the message never quotes the stripped name");
}

void checkSuccessThenReset(KindFixture const& kind) {
  resetRecorded();
  bool threw = false;
  try {
    kind.setter(kind.successName);
  } catch (...) {
    threw = true;
  }
  check(!threw, kind.requestName + " \"" + kind.successName + "\": the setter throws nothing");
  check(lastCode == RIE_NOERROR, kind.requestName + " \"" + kind.successName + "\": no error is reported");
  check(kind.hasValue(), kind.requestName + " \"" + kind.successName + "\": the getter reports a shader");
  if (kind.liveCount) {
    check(kind.liveCount() == 1, kind.requestName + " \"" + kind.successName + "\": the live count reads 1");
  }

  resetRecorded();
  bool resetThrew = false;
  try {
    kind.setter("nosuchshader");
  } catch (...) {
    resetThrew = true;
  }
  check(!resetThrew, kind.requestName + ": resetting with an unknown name throws nothing");
  check(!kind.hasValue(), kind.requestName + ": the getter reports nothing after a failed reset");
  if (kind.liveCount) {
    check(kind.liveCount() == 0, kind.requestName + ": the live count returns to 0 after the reset");
  }
}

void checkKind(std::function<KindFixture()> const& makeFixture) {
  checkFailure(makeFixture(), "nosuchshader", "no module");
  checkFailure(makeFixture(), "gmanzbuffer", "opens, no shader");
  checkFailure(makeFixture(), "nodestroyshader", "missing GMANDestroyShader", "GMANDestroyShader");
  checkFailure(makeFixture(), RI_MATTE, "wrong type");
  checkSuccessThenReset(makeFixture());
  checkDotSoNotSniffed(makeFixture());
}

} // namespace

int main() {
  RiErrorHandler(recordingErrorHandler);

  checkSurfaceUnchanged();

  checkKind(displacementFixture);
  checkKind(atmosphereFixture);
  checkKind(interiorFixture);
  checkKind(exteriorFixture);
  checkKind(imagerFixture);

  return checkSummary("every shader kind resolves its name to lib<name>.so and falls back on failure");
}
