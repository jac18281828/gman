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
 * Every shader kind -- Surface, Displacement, Atmosphere, Interior,
 * Exterior and Imager -- resolves its token to lib<name>.so and falls back
 * to a clean, unset state on any failure. Drives GMANAttributes and
 * GMANOptions directly, with a recording RiErrorHandler installed once.
 */

#include <functional>
#include <memory>
#include <string>

#include <dlfcn.h>

#include "check.h"
#include "gmanattributes.h"
#include "gmanloadableshader.h"
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

// countingvolume is a plugin, dlopened at runtime by GMANAttributes::
// setAtmosphere, never linked into this binary, so its own live-count
// function is resolved the same way: dlopen (idempotent) then dlsym, once,
// memoized here.
int countingVolumeLiveCount() {
  using LiveCountFn = int (*)();
  static LiveCountFn const fn = []() -> LiveCountFn {
    void* const handle = dlopen("libcountingvolume.so", RTLD_LAZY);
    if (handle == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<LiveCountFn>(dlsym(handle, "GMANCountingVolumeLiveCount"));
  }();
  return fn ? fn() : -1;
}

// ---- Surface: setSurface maps every name uniformly and quotes it
// verbatim in its report. ----

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
  kind.successName = "countingvolume";
  kind.setter = [attr](std::string const& name) { attr->setAtmosphere(name, GMANParameterList()); };
  kind.hasValue = [attr]() { return attr->getAtmosphere(0.0) != nullptr; };
  kind.liveCount = countingVolumeLiveCount;
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

// wrongTypeExactMessage, when not empty, pins the wrong-type report's exact
// text: a changed noun phrase for this kind's expected type turns this
// check red.
void checkKind(std::function<KindFixture()> const& makeFixture, std::string const& wrongTypeExactMessage = "") {
  checkFailure(makeFixture(), "nosuchshader", "no module");
  checkFailure(makeFixture(), "gmanzbuffer", "opens, no shader");
  checkFailure(makeFixture(), "nodestroyshader", "missing GMANDestroyShader", "GMANDestroyShader");
  {
    KindFixture const kind = makeFixture();
    checkFailure(kind, RI_MATTE, "wrong type");
    if (!wrongTypeExactMessage.empty()) {
      check(lastMessage == wrongTypeExactMessage, kind.requestName + " wrong type: the message matches exactly");
    }
  }
  checkSuccessThenReset(makeFixture());
  checkDotSoNotSniffed(makeFixture());
}

// No RI request resolves a light source through the shared resolver, so
// the public entry point is called directly: a wrong-type module names the
// light-source kind in its report.
void checkLightSourceWrongType() {
  resetRecorded();
  auto const resolved = gman::resolveLoadableShader("LightSource", "no light shines", "matte", GMANParameterList(),
                                                    GMANShader::LIGHTSOURCE);
  check(resolved == nullptr, "LightSource wrong type: nothing resolves");
  check(lastCode == RIE_NOSHADER && lastSeverity == RIE_ERROR, "LightSource wrong type: RIE_NOSHADER at RIE_ERROR");
  std::string const ending = "is not a light source shader.";
  check(lastMessage.size() >= ending.size() &&
            lastMessage.compare(lastMessage.size() - ending.size(), ending.size(), ending) == 0,
        "LightSource wrong type: the message names a light source");
}

} // namespace

int main() {
  RiErrorHandler(recordingErrorHandler);

  checkSurfaceUnchanged();

  checkKind(displacementFixture, "Displacement \"matte\" failed to load; the surface renders undisplaced: Specified "
                                 "displacement shader is not a displacement shader.");
  checkKind(atmosphereFixture, "Atmosphere \"matte\" failed to load; no atmosphere shades the volume: Specified "
                               "atmosphere shader is not a volume shader.");
  checkKind(interiorFixture);
  checkKind(exteriorFixture);
  checkKind(imagerFixture, "Imager \"matte\" failed to load; the frame renders without an imager: Specified imager "
                           "shader is not an imager shader.");
  checkLightSourceWrongType();

  return checkSummary("every shader kind resolves its name to lib<name>.so and falls back on failure");
}
