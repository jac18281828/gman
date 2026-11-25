/* SPDX-License-Identifier: LGPL-2.1-or-later */

/* This is part of GMAN, a RenderMan-compatible renderer.
 *
 * Copyright (c) 2001, 2000, 1999  John Cairns
 *
 * Author: John Cairns <john@2ad.com>
 */

/*
 * LJL -- Added calls to graphic state manager
 */

/*
 * JAC -- REMOVED THEM!
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

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gmanbasis.h"
#include "gmancolor.h"
#include "gmandefaults.h"
#include "gmandictionary.h"
#include "gmanerror.h"
#include "gmanfiledrivers.h"
#include "gmangraphicstate.h"
#include "gmanimagerpass.h"
#include "gmanimagershader.h"
#include "gmaninlineparse.h"
#include "gmanlightsourcemgr.h"
#include "gmanloadablerenderer.h"
#include "gmanlog.h"
#include "gmanmath.h"
#include "gmanmatrix4.h"
#include "gmanoptions.h"
#include "gmanoutput.h"
#include "gmanoutputx11.h"
#include "gmanparameterlist.h"
#include "gmanpoint.h"
#include "gmanpolygonmesh.h"
#include "gmanpolygonmeshfactory.h"
#include "gmanprimitive.h"
#include "gmanrenderer.h"
#include "gmanrendermanimpl.h"
#include "gmantexture.h"
#include "gmantrimcurve.h"
#include "gmanvector.h"
#include "gmanviewingsystem.h"
#include "gmanviewingsysteminputs.h"
#include "ri.h"

namespace {

// Option "render" "string indirect" ["<name>"]'s declaration token, type
// included: RiOptionV matches it literally, so a scene that never names
// it triggers no GMANParameterList construction at all.
constexpr char const* kIndirectPassToken = "string indirect";

// Option "radiosity" "float elementsize" ["<size>"]'s declaration
// token, matched the same literal way beside kIndirectPassToken.
constexpr char const* kElementSizeToken = "float elementsize";

// Option "pathtracer" "integer samples" ["<n>"]'s declaration token, matched
// the same literal way beside kIndirectPassToken and kElementSizeToken.
constexpr char const* kSamplesToken = "integer samples";

// Scans an Option's (token, parm) pairs for the exact declaration token
// and, on a match, wraps that one pair in a GMANParameterList. Returns
// false, leaving matched untouched, when the token is absent.
bool matchOptionToken(GMANDictionary& dictionary, RtInt n, RtToken tokens[], RtPointer parms[], char const* token,
                      GMANParameterList& matched) {
  for (RtInt i = 0; i < n; ++i) {
    if (std::string(tokens[i]) == token) {
      matched = GMANParameterList(dictionary, 1, &tokens[i], &parms[i]);
      return true;
    }
  }
  return false;
}

// Option "render" "string indirect" ["<name>"]: stores the name, ignoring
// a parameter list lacking that exact token.
void handleRenderOption(GMANDictionary& dictionary, RtInt n, RtToken tokens[], RtPointer parms[],
                        GMANOptions& options) {
  GMANParameterList p;
  if (!matchOptionToken(dictionary, n, tokens, parms, kIndirectPassToken, p)) {
    return;
  }
  std::string const* const value = (std::string const*)p.getPointer(dictionary.getTokenId(kIndirectPassToken));
  if (value != nullptr) {
    options.setIndirectPass(value[0]);
  }
}

// Option "radiosity" "float elementsize" ["<size>"]: stores a finite,
// positive size; a value failing that test logs one warning and changes
// nothing.
void handleRadiosityOption(GMANDictionary& dictionary, RtInt n, RtToken tokens[], RtPointer parms[],
                           GMANOptions& options) {
  GMANParameterList p;
  if (!matchOptionToken(dictionary, n, tokens, parms, kElementSizeToken, p)) {
    return;
  }
  RtFloat const* const value = (RtFloat const*)p.getPointer(dictionary.getTokenId(kElementSizeToken));
  if (value == nullptr) {
    return;
  }
  if (!std::isfinite(value[0]) || value[0] <= 0) {
    warning("Option \"radiosity\" \"float elementsize\": {} is not finite and positive; ignored.", value[0]);
    return;
  }
  options.setRadiosityElementSize(value[0]);
}

// Option "pathtracer" "integer samples" ["<n>"]: stores a count of at least
// 1; a lower value logs one warning naming samples and the value and
// changes nothing.
void handlePathtracerOption(GMANDictionary& dictionary, RtInt n, RtToken tokens[], RtPointer parms[],
                            GMANOptions& options) {
  GMANParameterList p;
  if (!matchOptionToken(dictionary, n, tokens, parms, kSamplesToken, p)) {
    return;
  }
  RtInt const* const value = (RtInt const*)p.getPointer(dictionary.getTokenId(kSamplesToken));
  if (value == nullptr) {
    return;
  }
  if (value[0] < 1) {
    warning("Option \"pathtracer\" \"integer samples\": {} is below 1; ignored.", value[0]);
    return;
  }
  options.setPathtracerSamples(value[0]);
}

// True for the three pairs the typed handlers above read: "indirect" under
// "render", "elementsize" under "radiosity" and "samples" under "pathtracer",
// whatever type the token declares.
bool isTypedOption(std::string const& option, std::string const& identifier) {
  return (option == "render" && identifier == "indirect") || (option == "radiosity" && identifier == "elementsize") ||
         (option == "pathtracer" && identifier == "samples");
}

// The token's name without class, type or quantity.
std::string optionParameterIdentifier(std::string const& token) {
  gman::InlineParse parse;
  parse.parse(token);
  return parse.isInline() ? parse.getIdentifier() : token;
}

// Copies the declared number of elements out of the caller's array into a
// value of the declared storage type.
GMANOptions::NamedOptionValue copyOptionValue(GMANDictionary& dictionary, GMANTokenId id, RtPointer parm) {
  const auto count = static_cast<std::size_t>(dictionary.allocSize(id, 1, 1, 1, 1));
  switch (dictionary.getType(id)) {
  case GMANTokenEntry::STRING: {
    auto const* const source = static_cast<char const* const*>(parm);
    std::vector<std::string> strings;
    strings.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      strings.emplace_back(source[i] != nullptr ? source[i] : "");
    }
    return strings;
  }
  case GMANTokenEntry::INTEGER: {
    auto const* const source = static_cast<RtInt const*>(parm);
    return std::vector<RtInt>(source, source + count);
  }
  default: {
    auto const* const source = static_cast<RtFloat const*>(parm);
    return std::vector<RtFloat>(source, source + count);
  }
  }
}

// Stores every (token, parm) pair under the Option name except the pairs the
// typed handlers read. A pair whose token the dictionary cannot resolve, or
// whose value is null, is dropped without a report.
void storeNamedOptions(GMANDictionary& dictionary, std::string const& option, RtInt n, RtToken tokens[],
                       RtPointer parms[], GMANOptions& options) {
  for (RtInt i = 0; i < n; ++i) {
    if (parms[i] == nullptr) {
      continue;
    }
    try {
      std::string const identifier = optionParameterIdentifier(tokens[i]);
      if (isTypedOption(option, identifier)) {
        continue;
      }
      const auto id = dictionary.getTokenId(tokens[i]);
      options.setNamedOption(option, identifier, copyOptionValue(dictionary, id, parms[i]));
    } catch (GMANError&) {
      continue;
    }
  }
}

void requireRgbColorSamples(GMANOptions const& options, char const* request) {
  const RtInt samples = options.getColorSamples().getNumber();
  if (samples == DefaultColorSamples) {
    return;
  }
  throw GMANError(RIE_UNIMPLEMENT, RIE_ERROR,
                  std::format("{} with {} color samples is unsupported: GMAN shades in {}-component RGB.", request,
                              samples, DefaultColorSamples)
                      .c_str());
}

// count clamped into [0, INT_MAX]: a count too large for RtInt reaches the
// parameter list as INT_MAX, which its sizing rejects when a parameter
// depends on it.
RtInt saturatedCount(std::int64_t count) { return static_cast<RtInt>(std::clamp<std::int64_t>(count, 0, INT_MAX)); }

} // namespace

/*
 * RenderMan API RiRenderMan
 *
 */

// default constructor
GMANRenderManImpl::GMANRenderManImpl() : GMANGraphicState() {
  objectManager = NULL;
  worldManager = NULL;
};

// default destructor
GMANRenderManImpl::~GMANRenderManImpl() {};

// declare shading language variable
RtToken GMANRenderManImpl::RiDeclare(const char* name, const char* declaration) {
  gman::InlineParse ip;
  std::string a(name);
  std::string b(declaration);

  b += " ";
  b += a;
  ip.parse(b);
  dictionary.addToken(ip.getIdentifier(), ip.getClass(), ip.getType(), ip.getQuantity(), false);
  return const_cast<char*>(name);
}

/* RenderMan state machine */

// begin new renderman rendering instance
RtVoid GMANRenderManImpl::RiBegin(RtToken name) {
  noRendererReported = false;
  releaseRenderer();

  if (name == NULL) {
    name = "gmanzbuffer";
  }

  //
  // Load the renderer from a shared object by name
  std::string objectName = "lib";
  objectName += name;
  objectName += ".so";
  renderer = std::make_unique<GMANLoadableRenderer>(objectName.c_str());
  worldManager = renderer->getWorldManager();

  objectManager = renderer->getObjectManager();
}

RtVoid GMANRenderManImpl::RiEnd(RtVoid) {
  noRendererReported = false;
  releaseRenderer();
}

RtVoid GMANRenderManImpl::releaseRenderer() {
  renderer.reset();
  // worldManager and objectManager belong to renderer, not to this object;
  // deleting it invalidates them too.
  worldManager = NULL;
  objectManager = NULL;
  viewingSystem.reset();
}

RtVoid GMANRenderManImpl::reportNoRendererOnce() {
  if (noRendererReported) {
    return;
  }
  noRendererReported = true;
  throw GMANError(RIE_NOTSTARTED, RIE_SEVERE, "No renderer loaded.");
}

RtVoid GMANRenderManImpl::RiFrameBegin(RtInt /*frame*/) { enterMode(F); }

RtVoid GMANRenderManImpl::RiFrameEnd(RtVoid) { leaveMode(F); }

RtVoid GMANRenderManImpl::RiWorldBegin(RtVoid) {
  enterMode(W);
  forgetWarningsOnce();
  gman::textureCache().clear();

  // RiProjection is legal only before RiWorldBegin, so the CTM right here
  // *is* the world-to-camera transform -- nothing after this point can
  // still be camera setup. RenderMan's camera looks down +z (left-handed);
  // GMANMatrix4::prjPersp relies on that to give w=z its sign.
  GMANMatrix4 worldToCamera = getTransform().interpolate(0.0);

  // FIXME FIXME FIXME
  // this order is messed up!!!
  GMANOptions::RasterInfo ri = getOptions().getRasterInfo();

  // Built locally and only handed to the `output` member once every
  // fallible step below (including viewing-system construction, which can
  // throw on a singular worldToCamera) has succeeded, so a GMANError
  // unwinding through here can't leak the driver.
  std::unique_ptr<GMANOutput> newOutput;

  if (getOptions().getDisplay().type == "file") {
    size_t startExt = getOptions().getDisplay().name.rfind(".");
    std::string ext;
    if (startExt != std::string::npos) {
      ext = getOptions().getDisplay().name.substr(startExt + 1);
    }
    debug("Displaying to file with extension, {}.", ext.c_str());

    // Throws RIE_BADFILE for a known but disabled driver's extension, or
    // an unrecognized one -- either way newOutput never reaches
    // RiWorldEnd null.
    newOutput = gman::makeFileOutput(ext, getOptions().getDisplay().name.c_str(),
                                     ri.rxmax - ri.rxmin + 1, // if the user use RiCropWindow
                                     ri.rymax - ri.rymin + 1);

    // Stops a request the driver cannot store here, before the frame renders.
    gman::checkQuantize(*newOutput, gman::quantizeRequest(*newOutput, getOptions()));

  } else { // type == framebuffer
    // our frame buffer
    newOutput.reset(new gman::OutputX11(getOptions().getDisplay().name.c_str(),
                                        ri.rxmax - ri.rxmin + 1, // if the user use RiCropWindow
                                        ri.rymax - ri.rymin + 1));
  }

  // The inputs both this renderer and the z-buffer polygon dicer
  // (GMANPatchPolyObjectManager) build their own viewing system from, so a
  // triangle's raster-space size matches what actually renders. dictionary
  // is the same one RiProjectionV resolved the projection's parameter list
  // through, so a redeclared "fov" still resolves here. The warning fires
  // once, before either projection branch, per RiWorldBegin.
  gman::ViewingSystemInputs const vsi = gman::resolveViewingSystemInputs(getOptions(), dictionary);
  if (vsi.fovDefaulted) {
    warning("FOV not set, defaulting to 90.0.");
  }

  viewingSystem = gman::makeViewingSystem(vsi, worldToCamera);

  // The shading path's only route to camera-to-world (this task's own
  // goal, AGENTS.md's "Handedness and matrix convention"): the same
  // inverse GMANViewingSystem already computed, recorded once here rather
  // than inverted again downstream. optionsStack is not pushed on
  // entering World (GMANGraphicState::enterMode), so every getRS* call
  // for a primitive declared in this world block reads the same
  // GMANOptions this sets.
  getOptions().setCameraToWorld(viewingSystem->getCameraToWorld());

  output = std::move(newOutput);
}

RtVoid GMANRenderManImpl::abandonWorld() {
  output.reset();
  leaveMode(W);
}

RtVoid GMANRenderManImpl::RiWorldEnd(RtVoid) {
  if (!renderer) {
    abandonWorld();
    reportNoRendererOnce();
    return;
  }

  // Every renderer reads pixel (0, 0) as its background, so it needs a
  // frame buffer with at least one pixel. With no Display, or a framebuffer
  // one, RiWorldBegin builds gman::OutputX11's 0x0 stub; report that instead
  // of handing it to the renderer.
  if (output == nullptr || output->getWidth() == 0 || output->getHeight() == 0) {
    abandonWorld();
    throw GMANError(RIE_ILLSTATE, RIE_ERROR, "No display output to render into.");
  }

  GMANOutput::DisplayMode dMode;

  if (getOptions().getDisplay().mode == RI_RGB) {
    dMode = GMANOutput::RGB;
  } else if (getOptions().getDisplay().mode == RI_RGBA) {
    dMode = GMANOutput::RGBA;
  } else if (getOptions().getDisplay().mode == RI_RGBAZ) {
    dMode = GMANOutput::RGBAZ;
  } else if (getOptions().getDisplay().mode == RI_A) {
    dMode = GMANOutput::A;
  } else if (getOptions().getDisplay().mode == RI_AZ) {
    dMode = GMANOutput::AZ;
  } else if (getOptions().getDisplay().mode == RI_Z) {
    dMode = GMANOutput::Z;
  } else {
    abandonWorld();
    throw GMANError(RIE_ILLSTATE, RIE_ERROR, "Invalid token passed for display mode.");
  }

  const GMANOptions::ExposureStruct& exposure = getOptions().getExposure();
  renderer->render(output.get(), viewingSystem.get(), getOptions(), getAttributes());

  // renderer must be copied to support depth
  if (dMode != GMANOutput::RGBA) {
    warning("This display mode, {}, may "
            "not be fully supported at this time.",
            getOptions().getDisplay().mode.c_str());
  }

  // Colour's own Quantize request serves every mode: no driver writes
  // depth, so a "z" request stored by RiQuantize has nothing to reach.
  if (GMANImagerShader const* const imager = getOptions().getImager()) {
    gman::runImager(*output, *imager, getOptions());
    output->save(dMode, 1.0f, 1.0f, gman::quantizeRequest(*output, getOptions()));
  } else {
    output->save(dMode, exposure.gain, exposure.gamma, gman::quantizeRequest(*output, getOptions()));
  }

  abandonWorld();
}

// create a new object
RtObjectHandle GMANRenderManImpl::RiObjectBegin(RtVoid) {
  enterMode(O);
  return (RtObjectHandle)0;
}

// commit the most recently created object
RtVoid GMANRenderManImpl::RiObjectEnd(RtVoid) { leaveMode(O); }

RtVoid GMANRenderManImpl::RiObjectInstance(RtObjectHandle /*handle*/) {
  if (!allowed(cmdObjectInstance)) {
    return;
  }
}

RtVoid GMANRenderManImpl::RiAttributeBegin(RtVoid) { enterMode(A); }
RtVoid GMANRenderManImpl::RiAttributeEnd(RtVoid) { leaveMode(A); }

RtVoid GMANRenderManImpl::RiTransformBegin(RtVoid) { enterMode(T); }
RtVoid GMANRenderManImpl::RiTransformEnd(RtVoid) { leaveMode(T); }

RtVoid GMANRenderManImpl::RiSolidBegin(RtToken /*operation*/) { enterMode(S); }
RtVoid GMANRenderManImpl::RiSolidEnd(RtVoid) { leaveMode(S); }

RtVoid GMANRenderManImpl::RiMotionBeginV(RtInt n, RtFloat times[]) { enterMotion(n, times); }
RtVoid GMANRenderManImpl::RiMotionEnd(RtVoid) { leaveMode(M); }

// **************************************************************
// ******* ******* ******* CAMERA OPTIONS ******* ******* *******
// **************************************************************
RtVoid GMANRenderManImpl::RiFormat(RtInt xres, RtInt yres, RtFloat aspect) {
  if (!allowed(cmdFormat)) {
    return;
  }
  getOptions().setFormat(xres, yres, aspect);
}
RtVoid GMANRenderManImpl::RiFrameAspectRatio(RtFloat aspect) {
  if (!allowed(cmdFrameAspectRatio)) {
    return;
  }
  getOptions().setFrameAspectRatio(aspect);
}
RtVoid GMANRenderManImpl::RiScreenWindow(RtFloat left, RtFloat right, RtFloat bottom, RtFloat top) {
  if (!allowed(cmdScreenWindow)) {
    return;
  }
  getOptions().setScreenWindow(left, right, bottom, top);
}
RtVoid GMANRenderManImpl::RiCropWindow(RtFloat xmin, RtFloat xmax, RtFloat ymin, RtFloat ymax) {
  if (!allowed(cmdCropWindow)) {
    return;
  }
  getOptions().setCropWindow(xmin, xmax, ymin, ymax);
}
RtVoid GMANRenderManImpl::RiProjectionV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) {
  if (!allowed(cmdProjection)) {
    return;
  }
  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setProjection(name, p);
}
RtVoid GMANRenderManImpl::RiClipping(RtFloat hither, RtFloat yon) {
  if (!allowed(cmdClipping)) {
    return;
  }
  getOptions().setClipping(hither, yon);
}
RtVoid GMANRenderManImpl::RiDepthOfField(RtFloat fstop, RtFloat focallength, RtFloat focaldistance) {
  if (!allowed(cmdDepthOfField)) {
    return;
  }
  getOptions().setDepthOfField(fstop, focallength, focaldistance);
}
RtVoid GMANRenderManImpl::RiShutter(RtFloat min, RtFloat max) {
  if (!allowed(cmdShutter)) {
    return;
  }
  getOptions().setShutter(min, max);
}

// ***************************************************************
// ******* ******* ******* DISPLAY OPTIONS ******* ******* *******
// ***************************************************************
RtVoid GMANRenderManImpl::RiPixelVariance(RtFloat variation) {
  if (!allowed(cmdPixelVariance)) {
    return;
  }
  getOptions().setPixelVariance(variation);
}
RtVoid GMANRenderManImpl::RiPixelSamples(RtFloat xsamples, RtFloat ysamples) {
  if (!allowed(cmdPixelSamples)) {
    return;
  }

  // A RIB-supplied count has no defense of its own against a fractional
  // value or an enormous allocation. Round to the nearest integer sample
  // count and clamp to [1, kMaxPixelSamples] -- 16 per axis is a
  // reasonable engineering limit for this renderer's scale.
  const RtFloat kMaxPixelSamples = 16.0;
  xsamples = GMANRound(xsamples);
  ysamples = GMANRound(ysamples);
  if (xsamples < 1.0 || xsamples > kMaxPixelSamples) {
    warning("PixelSamples xsamples {:.0f} out of [1,{:.0f}], clamping.", xsamples, kMaxPixelSamples);
  }
  if (ysamples < 1.0 || ysamples > kMaxPixelSamples) {
    warning("PixelSamples ysamples {:.0f} out of [1,{:.0f}], clamping.", ysamples, kMaxPixelSamples);
  }
  xsamples = GMANClamp<RtFloat>(xsamples, 1.0, kMaxPixelSamples);
  ysamples = GMANClamp<RtFloat>(ysamples, 1.0, kMaxPixelSamples);
  getOptions().setPixelSamples(xsamples, ysamples);
}
RtVoid GMANRenderManImpl::RiPixelFilter(RtFilterFunc filterfunc, RtFloat xwidth, RtFloat ywidth) {
  if (!allowed(cmdPixelFilter)) {
    return;
  }

  // The resolve's support box (GMANSampleBuffer::resolve) tests each
  // sample's offset from the pixel center against half the filter
  // width. A width below 1/xsamples can place every legal sample
  // offset outside that box, so weightSum never leaves zero and the
  // pixel silently keeps its default-constructed (black) value: no
  // error, no diagnostic. This only bites at even sample counts --
  // odd counts always have a centre sample at offset 0, which is
  // always in support. A width of 1.0 is the narrowest that is
  // guaranteed to cover at least one full pixel of sample offsets
  // regardless of sample count, so floor any width below it, not
  // just non-positive ones.
  const RtFloat kMinFilterWidth = 1.0;
  if (xwidth < kMinFilterWidth) {
    warning("PixelFilter xwidth {:.3f} is non-positive or too narrow, "
            "clamping to {:.1f}.",
            xwidth, kMinFilterWidth);
    xwidth = kMinFilterWidth;
  }
  if (ywidth < kMinFilterWidth) {
    warning("PixelFilter ywidth {:.3f} is non-positive or too narrow, "
            "clamping to {:.1f}.",
            ywidth, kMinFilterWidth);
    ywidth = kMinFilterWidth;
  }
  getOptions().setPixelFilter(filterfunc, xwidth, ywidth);
}
RtVoid GMANRenderManImpl::RiExposure(RtFloat gain, RtFloat gamma) {
  if (!allowed(cmdExposure)) {
    return;
  }
  getOptions().setExposure(gain, gamma);
}
RtVoid GMANRenderManImpl::RiImagerV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) {
  if (!allowed(cmdImager)) {
    return;
  }
  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setImager(name, p);
}
RtVoid GMANRenderManImpl::RiQuantize(RtToken type, RtInt one, RtInt min, RtInt max, RtFloat ampl) {
  if (!allowed(cmdQuantize)) {
    return;
  }
  // strcmp, not pointer equality: type is a token parsed out of the RIB and
  // is never the RI_RGBA/RI_Z globals themselves. RiGeometricApproximation
  // compares the same kind of token with strcmp.
  if (strcmp(type, RI_RGBA) == 0) {
    getOptions().setColorQuantize(one, min, max, ampl);
    return;
  } else if (strcmp(type, RI_Z) == 0) {
    getOptions().setDepthQuantize(one, min, max, ampl);
    return;
  }
  GMANError error(RIE_UNIMPLEMENT, RIE_WARNING, "Unknown quantizer type");
  throw error;
}
RtVoid GMANRenderManImpl::RiDisplayV(char* name, RtToken type, RtToken mode, RtInt n, RtToken tokens[],
                                     RtPointer parms[]) {
  if (!allowed(cmdDisplay)) {
    return;
  }

  // RISpec: a name with no leading '+' replaces the display set; one
  // with a leading '+' adds to it. GMAN renders through a single active
  // display (see AGENTS.md's RIB support table) rather than a real set,
  // so "add" means "keep whichever of the two can actually write
  // output" instead of accumulating displays.
  std::string nm(name);
  bool add = !nm.empty() && nm[0] == '+';
  if (add) {
    nm.erase(0, 1);
  }

  bool haveDisplay = !getOptions().getDisplay().type.empty();
  bool replaces = !add || !haveDisplay;
  bool upgrades = add && haveDisplay && getOptions().getDisplay().type != "file" && std::string(type) == "file";

  if (!replaces && !upgrades) {
    return; // keep the currently active display
  }

  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setDisplay(nm, type, mode, p);
}

// ******************************************************************
// ******* ******* ******* ADDITIONAL OPTIONS ******* ******* *******
// ******************************************************************
RtVoid GMANRenderManImpl::RiHiderV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[]) {
  if (!allowed(cmdHider)) {
    return;
  }
  GMANParameterList p(dictionary, n, tokens, parms);
  getOptions().setHider(type, p);
}
RtVoid GMANRenderManImpl::RiColorSamples(RtInt n, RtFloat nRGB[], RtFloat RGBn[]) {
  if (!allowed(cmdColorSamples)) {
    return;
  }
  if (n < 1) {
    throw GMANError(RIE_RANGE, RIE_ERROR, std::format("ColorSamples needs at least one sample, got {}.", n).c_str());
  }
  getOptions().setColorSamples(n, nRGB, RGBn);
}
RtInt GMANRenderManImpl::colorSampleCount() const { return getOptions().getColorSamples().getNumber(); }
RtVoid GMANRenderManImpl::RiRelativeDetail(RtFloat relativedetail) {
  if (!allowed(cmdRelativeDetail)) {
    return;
  }
  getOptions().setRelativeDetail(relativedetail);
}
// Option "render" "string indirect" ["<name>"], Option "radiosity" "float
// elementsize" ["<size>"] and Option "pathtracer" "integer samples" ["<n>"]
// set their typed options, each only when it carries its exact token. Every
// other pair under any name, those three names included, is copied into the
// options' named store, keyed by Option name and parameter identifier, and
// read back through GMANOptions::getNamedOption. A token the dictionary cannot
// resolve is dropped silently. No allowed(cmdOption) call: that mask is all
// zero, so it would reject every Option the corpus already relies on.
RtVoid GMANRenderManImpl::RiOptionV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) {
  std::string const optionName(name);
  if (optionName == "render") {
    handleRenderOption(dictionary, n, tokens, parms, getOptions());
  } else if (optionName == "radiosity") {
    handleRadiosityOption(dictionary, n, tokens, parms, getOptions());
  } else if (optionName == "pathtracer") {
    handlePathtracerOption(dictionary, n, tokens, parms, getOptions());
  }
  storeNamedOptions(dictionary, optionName, n, tokens, parms, getOptions());
}

// ******************************************************************
// ******* ******* ******* SHADING ATTRIBUTES ******* ******* *******
// ******************************************************************
RtVoid GMANRenderManImpl::RiColor(RtColor color) {
  if (!allowed(cmdColor)) {
    return;
  }
  requireRgbColorSamples(getOptions(), "Color");
  getAttributes().setColor(color);
}
RtVoid GMANRenderManImpl::RiOpacity(RtColor color) {
  if (!allowed(cmdOpacity)) {
    return;
  }
  requireRgbColorSamples(getOptions(), "Opacity");
  getAttributes().setOpacity(color);
}
RtVoid GMANRenderManImpl::RiTextureCoordinates(RtFloat s1, RtFloat t1, RtFloat s2, RtFloat t2, RtFloat s3, RtFloat t3,
                                               RtFloat s4, RtFloat t4) {
  if (!allowed(cmdTextureCoordinates)) {
    return;
  }
  getAttributes().setTextureCoordinates(s1, t1, s2, t2, s3, t3, s4, t4);
}
namespace {

// GMANParameterList::getPointer returns NULL for a token this parameter
// list doesn't carry -- every light parameter here is optional, so probing
// for one is routine. The try/catch is a defensive remnant of when
// getPointer threw instead (the same defect threw against RI_FOV);
// harmless to keep, since a token this dictionary never declared at all
// would still throw out of getTokenId.
RtFloat* tryGetPointer(GMANDictionary& dictionary, GMANParameterList& pl, RtToken token) {
  try {
    return (RtFloat*)pl.getPointer(dictionary.getTokenId(token));
  } catch (GMANError&) {
    return NULL;
  }
}

// "intensity" (RtFloat) and "lightcolor" (RtColor), the two parameters the
// three built-in light types share.
RtVoid readCommonLightParams(GMANDictionary& dictionary, GMANParameterList& pl, RtFloat& intensity, GMANColor& color) {
  RtFloat* ip = tryGetPointer(dictionary, pl, RI_INTENSITY);
  if (ip) {
    intensity = ip[0];
  }
  RtFloat* cp = tryGetPointer(dictionary, pl, RI_LIGHTCOLOR);
  if (cp) {
    color = GMANColor(cp[0], cp[1], cp[2]);
  }
}

// RiLightSourceV and RiAreaLightSourceV's own shared draw: lightcolor times
// intensity, each defaulting to white and 1 where absent.
GMANColor readLightCl(GMANDictionary& dictionary, GMANParameterList& pl) {
  RtFloat intensity = 1.0;
  GMANColor color((RtFloat)1.0, (RtFloat)1.0, (RtFloat)1.0);
  readCommonLightParams(dictionary, pl, intensity, color);
  return GMANColor(color.getRed() * intensity, color.getGreen() * intensity, color.getBlue() * intensity);
}

// "from"/"to" (RtPoint), read with the RISpec's own defaults: from
// (0,0,0), to (0,0,1) -- a distantlight or pointlight with no explicit
// position or direction still declares something sane.
RtVoid readFromTo(GMANDictionary& dictionary, GMANParameterList& pl, GMANPoint& from, GMANPoint& to) {
  RtFloat* fp = tryGetPointer(dictionary, pl, RI_FROM);
  if (fp) {
    from = GMANPoint(fp[0], fp[1], fp[2]);
  }
  RtFloat* tp = tryGetPointer(dictionary, pl, RI_TO);
  if (tp) {
    to = GMANPoint(tp[0], tp[1], tp[2]);
  }
}

// "coneangle"/"conedeltaangle"/"beamdistribution" (RtFloat), all optional;
// the caller passes in the RISpec defaults to fall back on.
RtVoid readSpotConeParams(GMANDictionary& dictionary, GMANParameterList& pl, RtFloat& coneAngle,
                          RtFloat& coneDeltaAngle, RtFloat& beamDistribution) {
  RtFloat* cap = tryGetPointer(dictionary, pl, RI_CONEANGLE);
  if (cap) {
    coneAngle = cap[0];
  }
  RtFloat* cdap = tryGetPointer(dictionary, pl, RI_CONEDELTAANGLE);
  if (cdap) {
    coneDeltaAngle = cdap[0];
  }
  RtFloat* bdp = tryGetPointer(dictionary, pl, RI_BEAMDISTRIBUTION);
  if (bdp) {
    beamDistribution = bdp[0];
  }
}

} // namespace

RtLightHandle GMANRenderManImpl::RiLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) {
  if (!allowed(cmdLightSource)) {
    return (RtLightHandle)0;
  }
  GMANParameterList paramList(dictionary, n, tokens, parms);

  // Captured at declaration time, exactly the mechanism every primitive
  // uses (RiSphereV et al, near :803): the light's position/direction
  // travel through the CTM now in effect, landing in camera space to
  // match every other quantity shading math touches. A light is a point
  // or a direction, not a solid, so no inverse transpose is needed here --
  // that correction is for surface normals only.
  GMANTransform transform(getTransform());

  std::string lightName(name);
  GMANColor cl = readLightCl(dictionary, paramList);

  GMANLightType type;
  GMANPoint position(0.0, 0.0, 0.0);
  GMANVector direction(0.0, 0.0, 1.0);
  RtFloat coneAngle = 0.0, coneDeltaAngle = 0.0, beamDistribution = 0.0;

  if (lightName == "ambientlight") {
    type = GMAN_LIGHT_AMBIENT;
  } else if (lightName == "distantlight") {
    type = GMAN_LIGHT_DISTANT;
    GMANPoint from(0.0, 0.0, 0.0);
    GMANPoint to(0.0, 0.0, 1.0);
    readFromTo(dictionary, paramList, from, to);
    GMANPoint camFrom = transform.apply(from);
    GMANPoint camTo = transform.apply(to);
    direction = GMANVector(camFrom, camTo);
    direction.normalize();
  } else if (lightName == "pointlight") {
    type = GMAN_LIGHT_POINT;
    GMANPoint from(0.0, 0.0, 0.0);
    GMANPoint to(0.0, 0.0, 1.0);
    readFromTo(dictionary, paramList, from, to);
    position = transform.apply(from);
  } else if (lightName == "spotlight") {
    type = GMAN_LIGHT_SPOT;
    GMANPoint from(0.0, 0.0, 0.0);
    GMANPoint to(0.0, 0.0, 1.0);
    readFromTo(dictionary, paramList, from, to);
    position = transform.apply(from);
    GMANPoint camTo = transform.apply(to);
    direction = GMANVector(position, camTo);
    direction.normalize();

    // RISpec defaults: 30 degrees, 5 degrees and 2 -- radians throughout
    // per the RISpec, not degrees: a scene's "coneangle" [30] means 30
    // radians, not 30 degrees.
    coneAngle = (RtFloat)0.5235987756;
    coneDeltaAngle = (RtFloat)0.0872664626;
    beamDistribution = (RtFloat)2.0;
    readSpotConeParams(dictionary, paramList, coneAngle, coneDeltaAngle, beamDistribution);
  } else {
    warning("Unknown light shader '{}'; ignoring RiLightSource.", lightName.c_str());
    return (RtLightHandle)0;
  }

  GMANLight* light = new GMANLight(type, cl, position, direction, coneAngle, coneDeltaAngle, beamDistribution);
  RtLightHandle handle = gmanLightSourceMgr().add(light);

  // Per the RISpec: a light is active in the current graphics state the
  // instant it is declared, as if RiIlluminate(handle, RI_TRUE) had just
  // been called. Without this, GMANAttributes::lightList never carries
  // the handle and no light this call creates ever reaches a shader --
  // RiIlluminate only needs to be called explicitly to turn a light back
  // on after RiIlluminate(handle, RI_FALSE) turned it off, or to bring an
  // outer-scope light into a nested attribute block.
  getAttributes().setIlluminate(handle, RI_TRUE);

  return handle;
}
RtLightHandle GMANRenderManImpl::RiAreaLightSourceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) {
  if (!allowed(cmdAreaLightSource)) {
    return (RtLightHandle)0;
  }
  GMANParameterList paramList(dictionary, n, tokens, parms);

  // Case-sensitive, unlike the AreaLightSource request keyword itself:
  // RiLightSourceV's own four built-in names are matched the same way,
  // never case-insensitively.
  std::string lightName(name);
  if (lightName != "arealight") {
    warning("Unknown area light shader '{}'; ignoring RiAreaLightSource.", lightName.c_str());
    return (RtLightHandle)0;
  }

  GMANColor cl = readLightCl(dictionary, paramList);

  // No position, direction or cone parameters: a sphere or disk emitter's
  // placement comes from its own geometry, so this light's position and
  // direction stay at GMANLight's own defaults, unused.
  const auto light = new GMANLight(GMAN_LIGHT_AREA, cl, GMANPoint(0.0, 0.0, 0.0), GMANVector(0.0, 0.0, 1.0));
  RtLightHandle handle = gmanLightSourceMgr().add(light);

  // Active the instant it is declared, exactly as RiLightSourceV's own
  // handle is: getAttributes().getLightList() already gives RiIlluminate
  // and AttributeBegin/AttributeEnd their scoping for free.
  getAttributes().setIlluminate(handle, RI_TRUE);
  getAttributes().setAreaLight(handle);

  return handle;
}
RtVoid GMANRenderManImpl::RiIlluminate(RtLightHandle light, RtBoolean onoff) {
  if (!allowed(cmdIlluminate)) {
    return;
  }
  getAttributes().setIlluminate(light, onoff);
}
RtVoid GMANRenderManImpl::RiSurfaceV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) {
  if (!allowed(cmdSurface)) {
    return;
  }
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setSurface(name, p);
}
RtVoid GMANRenderManImpl::RiAtmosphereV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) {
  if (!allowed(cmdAtmosphere)) {
    return;
  }
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setAtmosphere(name, p);
}
RtVoid GMANRenderManImpl::RiInteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) {
  if (!allowed(cmdInterior)) {
    return;
  }
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setInterior(name, p);
}
RtVoid GMANRenderManImpl::RiExteriorV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) {
  if (!allowed(cmdExterior)) {
    return;
  }
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setExterior(name, p);
}
RtVoid GMANRenderManImpl::RiShadingRate(RtFloat size) {
  if (!allowed(cmdShadingRate)) {
    return;
  }
  getAttributes().setShadingRate(size);
}
RtVoid GMANRenderManImpl::RiShadingInterpolation(RtToken type) {
  if (!allowed(cmdShadingInterpolation)) {
    return;
  }
  if (strcmp(type, RI_CONSTANT) && strcmp(type, RI_SMOOTH)) {
    GMANError error(RIE_UNIMPLEMENT, RIE_WARNING, "Unknown shading interpolation type");
    throw error;
  }
  getAttributes().setShadingInterpolation(type);
}
RtVoid GMANRenderManImpl::RiMatte(RtBoolean onoff) {
  if (!allowed(cmdMatte)) {
    return;
  }
  getAttributes().setMatte(onoff);
}

// *******************************************************************
// ******* ******* ******* GEOMETRY ATTRIBUTES ******* ******* *******
// *******************************************************************
RtVoid GMANRenderManImpl::RiBound(RtBound b) {
  if (!allowed(cmdBound)) {
    return;
  }
  getAttributes().setBound(b);
}
RtVoid GMANRenderManImpl::RiDetail(RtBound d) {
  if (!allowed(cmdDetail)) {
    return;
  }
  getAttributes().setDetail(d);
}
RtVoid GMANRenderManImpl::RiDetailRange(RtFloat minvis, RtFloat lowtran, RtFloat uptran, RtFloat maxvis) {
  if (!allowed(cmdDetailRange)) {
    return;
  }
  getAttributes().setDetailRange(minvis, lowtran, uptran, maxvis);
}
RtVoid GMANRenderManImpl::RiGeometricApproximation(RtToken type, RtFloat value) {
  if (!allowed(cmdGeometricApproximation)) {
    return;
  }
  // strcmp, not pointer equality: type is a token parsed out of the RIB and
  // is never the RI_FLATNESS global itself, so the pointer test rejected
  // every value including the only legal one. RiShadingInterpolation and
  // RiOrientation just below compare the same kind of token with strcmp.
  if (strcmp(type, RI_FLATNESS)) {
    GMANError error(RIE_UNIMPLEMENT, RIE_WARNING, "Unknown geometric approximation type");
    throw error;
  }
  getAttributes().setGeometricApproximation(type, value);
}
RtVoid GMANRenderManImpl::RiBasis(RtBasis ubasis, RtInt ustep, RtBasis vbasis, RtInt vstep) {
  if (!allowed(cmdBasis)) {
    return;
  }
  getAttributes().setUVBasis(ubasis, ustep, vbasis, vstep);
}
RtVoid GMANRenderManImpl::RiTrimCurve(RtInt nloops, RtInt ncurves[], RtInt order[], RtFloat knot[], RtFloat min[],
                                      RtFloat max[], RtInt n[], RtFloat u[], RtFloat v[], RtFloat w[]) {
  if (!allowed(cmdTrimCurve)) {
    return;
  }
  GMANTrimCurve tc(nloops, ncurves, order, knot, min, max, n, u, v, w);
  getAttributes().setTrimCurves(tc);
}
RtVoid GMANRenderManImpl::RiOrientation(RtToken o) {
  if (!allowed(cmdOrientation)) {
    return;
  }
  if (strcmp(o, RI_INSIDE) && strcmp(o, RI_OUTSIDE) && strcmp(o, RI_LH) && strcmp(o, RI_RH)) {
    GMANError error(RIE_UNIMPLEMENT, RIE_WARNING, "Unknown orientation type");
    throw error;
  }
  getAttributes().setOrientation(o);
}

RtVoid GMANRenderManImpl::RiReverseOrientation(RtVoid) {
  if (!allowed(cmdReverseOrientation)) {
    return;
  }
  getAttributes().toggleOrientation();
}

RtVoid GMANRenderManImpl::RiSides(RtInt sides) {
  if (!allowed(cmdSides)) {
    return;
  }
  getAttributes().setSides(sides);
}
RtVoid GMANRenderManImpl::RiDisplacementV(RtToken name, RtInt n, RtToken tokens[], RtPointer parms[]) {
  if (!allowed(cmdDisplacement)) {
    return;
  }
  GMANParameterList p(dictionary, n, tokens, parms);
  getAttributes().setDisplacement(name, p);
}

// ***************************************************************
// ******* ******* ******* TRANSFORMATIONS ******* ******* *******
// ***************************************************************
RtVoid GMANRenderManImpl::RiIdentity(RtVoid) {
  if (!allowed(cmdIdentity)) {
    return;
  }
  GMANMatrix4 m;
  setTransform(m);
}
RtVoid GMANRenderManImpl::RiTransform(RtMatrix transform) {
  if (!allowed(cmdTransform)) {
    return;
  }
  GMANMatrix4 m(transform);
  setTransform(m);
}
RtVoid GMANRenderManImpl::RiConcatTransform(RtMatrix transform) {
  if (!allowed(cmdConcatTransform)) {
    return;
  }
  GMANMatrix4 m(transform);
  buildTransform(m);
}
RtVoid GMANRenderManImpl::RiPerspective(RtFloat fov) {
  if (!allowed(cmdPerspective)) {
    return;
  }
  GMANMatrix4 m;
  m.persp(fov);
  buildTransform(m);
}
RtVoid GMANRenderManImpl::RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz) {
  if (!allowed(cmdTranslate)) {
    return;
  }
  GMANMatrix4 m;
  m.trans(dx, dy, dz);
  buildTransform(m);
}
RtVoid GMANRenderManImpl::RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz) {
  if (!allowed(cmdRotate)) {
    return;
  }
  GMANMatrix4 m;
  m.rot(angle * DEGTORAD, dx, dy, dz);
  buildTransform(m);
}
RtVoid GMANRenderManImpl::RiScale(RtFloat sx, RtFloat sy, RtFloat sz) {
  if (!allowed(cmdScale)) {
    return;
  }
  GMANMatrix4 m;
  m.scale(sx, sy, sz);
  buildTransform(m);
}
RtVoid GMANRenderManImpl::RiSkew(RtFloat angle, RtFloat dx1, RtFloat dy1, RtFloat dz1, RtFloat dx2, RtFloat dy2,
                                 RtFloat dz2) {
  if (!allowed(cmdSkew)) {
    return;
  }
  GMANMatrix4 m;
  GMANVector a(dx1, dy1, dz1);
  GMANVector b(dx2, dy2, dz2);
  m.skew(angle, a, b);
  buildTransform(m);
}
RtVoid GMANRenderManImpl::RiDeformationV(RtToken /*name*/, RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[]) {}
RtVoid GMANRenderManImpl::RiCoordinateSystem(RtToken /*space*/) {
  if (!allowed(cmdCoordinateSystem)) {
    return;
  }
}
RtVoid GMANRenderManImpl::RiCoordSysTransform(RtToken /*space*/) {
  if (!allowed(cmdCoordSysTransform)) {
    return;
  }
}

RtPoint* GMANRenderManImpl::RiTransformPoints(RtToken /*fromspace*/, RtToken /*tospace*/, RtInt /*n*/,
                                              RtPoint /*points*/[]) {
  if (!allowed(cmdTransformPoints)) {
    return nullptr;
  }
  return (RtPoint*)0;
}

// AttributeV
RtVoid GMANRenderManImpl::RiAttributeV(RtToken /*name*/, RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[]) {}

// **********************************************************
// ******* ******* ******* PRIMITIVES ******* ******* *******
// **********************************************************
RtVoid GMANRenderManImpl::RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[]) {
  RiPolygonV(nverts, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiPolygonV(RtInt nverts, RtInt n, RtToken tokens[], RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdPolygon)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }

  std::optional<GMANPolygonMesh> const mesh = gman::polygonMesh(nverts, dictionary, n, tokens, parms, counts);
  if (!mesh) {
    worldManager->add(objectManager->create());
    return;
  }

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSPolygonMesh(*mesh, &(getOptions()), &(getAttributes()), &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiGeneralPolygonV(RtInt nloops, RtInt nverts[], RtInt n, RtToken tokens[],
                                            RtPointer parms[]) {
  RiGeneralPolygonV(nloops, nverts, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiGeneralPolygonV(RtInt nloops, RtInt nverts[], RtInt n, RtToken tokens[], RtPointer parms[],
                                            const RtInt* counts) {
  if (!allowed(cmdGeneralPolygon)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }

  std::optional<GMANPolygonMesh> const mesh =
      gman::generalPolygonMesh(nloops, nverts, dictionary, n, tokens, parms, counts);
  if (!mesh) {
    worldManager->add(objectManager->create());
    return;
  }

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSPolygonMesh(*mesh, &(getOptions()), &(getAttributes()), &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiPointsPolygonsV(RtInt npolys, RtInt nverts[], RtInt verts[], RtInt n, RtToken tokens[],
                                            RtPointer parms[]) {
  RiPointsPolygonsV(npolys, nverts, verts, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiPointsPolygonsV(RtInt npolys, RtInt nverts[], RtInt verts[], RtInt n, RtToken tokens[],
                                            RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdPointsPolygon)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }

  std::optional<GMANPolygonMesh> const mesh =
      gman::pointsPolygonsMesh(npolys, nverts, verts, dictionary, n, tokens, parms, counts);
  if (!mesh) {
    worldManager->add(objectManager->create());
    return;
  }

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSPolygonMesh(*mesh, &(getOptions()), &(getAttributes()), &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiPointsGeneralPolygonsV(RtInt npolys, RtInt nloops[], RtInt nverts[], RtInt verts[], RtInt n,
                                                   RtToken tokens[], RtPointer parms[]) {
  RiPointsGeneralPolygonsV(npolys, nloops, nverts, verts, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiPointsGeneralPolygonsV(RtInt npolys, RtInt nloops[], RtInt nverts[], RtInt verts[], RtInt n,
                                                   RtToken tokens[], RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdPointsGeneralPolygons)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }

  std::optional<GMANPolygonMesh> const mesh =
      gman::pointsGeneralPolygonsMesh(npolys, nloops, nverts, verts, dictionary, n, tokens, parms, counts);
  if (!mesh) {
    worldManager->add(objectManager->create());
    return;
  }

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSPolygonMesh(*mesh, &(getOptions()), &(getAttributes()), &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[]) {
  RiPatchV(type, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiPatchV(RtToken type, RtInt n, RtToken tokens[], RtPointer parms[], const RtInt* counts) {
  if (!allowed(cmdPatch)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }

  // A bicubic patch carries sixteen vertex-class "P" values, not four;
  // sizing this for every type as bilinear's four truncates "P" and reads
  // past it. An unrecognized type gets the conservative floor and is
  // rejected by getRSPatch's own fallback, never sized for sixteen.
  RtInt vertex = (strcmp(type, RI_BICUBIC) == 0) ? 16 : 4;
  GMANParameterList paramList(dictionary, n, tokens, parms, vertex, 4, 1, 1, counts);

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSPatch(type, paramList, &(getOptions()), &(getAttributes()), &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiPatchMeshV(RtToken type, RtInt nu, RtToken uwrap, RtInt nv, RtToken vwrap, RtInt n,
                                       RtToken tokens[], RtPointer parms[]) {
  RiPatchMeshV(type, nu, uwrap, nv, vwrap, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiPatchMeshV(RtToken type, RtInt nu, RtToken uwrap, RtInt nv, RtToken vwrap, RtInt n,
                                       RtToken tokens[], RtPointer parms[], const RtInt* counts) {
  if (!allowed(cmdPatchMesh)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }

  bool uPeriodic = strcmp(uwrap, RI_PERIODIC) == 0;
  bool vPeriodic = strcmp(vwrap, RI_PERIODIC) == 0;
  bool bicubicType = strcmp(type, RI_BICUBIC) == 0;

  // Bilinear's step is fixed at 1 by the RISpec; bicubic reads its step
  // off the current basis, which RiBasis sets and getRSPatchMesh resolves
  // the same way. nupatches/nvpatches mirror the sub-patch count that
  // evaluator actually iterates over -- getRSPatchMesh rejects an nu/nv
  // its own arithmetic cannot align to the current basis step, but that
  // check is over nu/nv alone; the parameter list below confirms the
  // RIB-supplied "P" array actually holds nu*nv points, and stops the file
  // with RIE_CONSISTENCY when it holds fewer. Clamping a negative count to
  // zero here only keeps this sizing arithmetic itself from going negative,
  // never anyone else's.
  RtInt uStep = 1, vStep = 1;
  if (bicubicType) {
    GMANBasis basis = getAttributes().getUVBasis();
    uStep = basis.getUStep();
    vStep = basis.getVStep();
    // nupatches/nvpatches divide by uStep/vStep below; neither RiBasis nor
    // the RIB parser rejects a zero (or negative) step, and getRSPatchMesh's
    // own step<1 checks run after this division, too late to guard it.
    if (uStep < 1 || vStep < 1) {
      warning("PatchMesh \"bicubic\": basis step must be >= 1 (uStep={} "
              "vStep={}); ignoring.",
              uStep, vStep);
      worldManager->add(objectManager->create());
      return;
    }
  }

  // The counts below run in 64 bits and saturate at INT_MAX when they reach
  // the parameter list, so a mesh too large to count never wraps: the
  // list's sizing rejects any parameter that count sizes.
  const std::int64_t u = nu;
  const std::int64_t v = nv;
  std::int64_t nupatches = bicubicType ? (uPeriodic ? u / uStep : 1 + (u - 4) / uStep) : (uPeriodic ? u : u - 1);
  std::int64_t nvpatches = bicubicType ? (vPeriodic ? v / vStep : 1 + (v - 4) / vStep) : (vPeriodic ? v : v - 1);
  if (nupatches < 0) {
    nupatches = 0;
  }
  if (nvpatches < 0) {
    nvpatches = 0;
  }

  // Vertex count is nu*nv for both types; varying is per-axis corner
  // count (patches+1 on a nonperiodic axis, patches on a periodic one,
  // wrapping the last strip back to row/column 0), multiplied; uniform is
  // the patch count outright.
  const std::int64_t uVarying = uPeriodic ? nupatches : nupatches + 1;
  const std::int64_t vVarying = vPeriodic ? nvpatches : nvpatches + 1;

  const RtInt vertex = saturatedCount(u * v);
  const RtInt varying = saturatedCount(uVarying * vVarying);
  const RtInt uniform = saturatedCount(nupatches * nvpatches);

  GMANParameterList paramList(dictionary, n, tokens, parms, vertex, varying, uniform, 1, counts);

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSPatchMesh(type, nu, uwrap, nv, vwrap, paramList, &(getOptions()), &(getAttributes()),
                                       &transform);
  worldManager->add(prim);
}
namespace {

// The predicates validateNuPatch shares between its u and v axes. Each
// axis keeps its own full warning literal, so the text a violation
// prints always names the axis that failed; only the test itself is
// shared here.
bool nuPatchOrderValid(RtInt n, RtInt order) { return order >= 1 && n >= order; }

// Index of the first knot less than its predecessor, or -1 when the
// length-(n+order) array is non-decreasing throughout.
RtInt firstDecreasingKnot(RtInt n, RtInt order, RtFloat const* knot) {
  for (RtInt i = 1; i < n + order; i++) {
    if (knot[i] < knot[i - 1]) {
      return i;
    }
  }
  return -1;
}

bool nuPatchRangeNonEmpty(RtFloat min, RtFloat max) { return min < max; }
bool nuPatchRangeAboveFloor(RtFloat min, RtFloat const* knot, RtInt order) { return min >= knot[order - 1]; }
bool nuPatchRangeBelowCeiling(RtFloat max, RtFloat const* knot, RtInt n) { return max <= knot[n]; }

// RiSpec 3.2's RiNuPatch rules, checked once before the parameter list is
// built: order positive and no greater than its own point count,
// non-decreasing knots, both range rules, and nu*nv*4 (the "Pw" case's own
// float count) fitting in RtInt. Warns once naming the rule and its values
// on the first violation and returns false; the caller adds
// objectManager->create() and returns, RiPatchMeshV's own "ignoring" shape.
// The first violated rule decides the one warning, so the sequence below
// -- order, overflow, knots, range -- each axis checked in turn, is part
// of the contract, not an implementation detail free to reorder.
bool validateNuPatch(RtInt nu, RtInt uorder, RtFloat const* uknot, RtFloat umin, RtFloat umax, RtInt nv, RtInt vorder,
                     RtFloat const* vknot, RtFloat vmin, RtFloat vmax) {
  if (!nuPatchOrderValid(nu, uorder)) {
    warning("NuPatch: nu={} uorder={} violates nu >= uorder >= 1; "
            "ignoring.",
            nu, uorder);
    return false;
  }
  if (!nuPatchOrderValid(nv, vorder)) {
    warning("NuPatch: nv={} vorder={} violates nv >= vorder >= 1; "
            "ignoring.",
            nv, vorder);
    return false;
  }
  if ((long long)nu * (long long)nv * 4 > (long long)INT_MAX) {
    warning("NuPatch: nu={} nv={}, times 4 overflows RtInt; ignoring.", nu, nv);
    return false;
  }
  RtInt uBadKnot = firstDecreasingKnot(nu, uorder, uknot);
  if (uBadKnot >= 0) {
    warning("NuPatch: uknot[{}]={} is less than uknot[{}]={}, not "
            "non-decreasing; ignoring.",
            uBadKnot, uknot[uBadKnot], uBadKnot - 1, uknot[uBadKnot - 1]);
    return false;
  }
  RtInt vBadKnot = firstDecreasingKnot(nv, vorder, vknot);
  if (vBadKnot >= 0) {
    warning("NuPatch: vknot[{}]={} is less than vknot[{}]={}, not "
            "non-decreasing; ignoring.",
            vBadKnot, vknot[vBadKnot], vBadKnot - 1, vknot[vBadKnot - 1]);
    return false;
  }
  if (!nuPatchRangeNonEmpty(umin, umax)) {
    warning("NuPatch: umin={} umax={} violates umin < umax; ignoring.", umin, umax);
    return false;
  }
  if (!nuPatchRangeAboveFloor(umin, uknot, uorder)) {
    warning("NuPatch: umin={} is less than uknot[uorder-1]={}; ignoring.", umin, uknot[uorder - 1]);
    return false;
  }
  if (!nuPatchRangeBelowCeiling(umax, uknot, nu)) {
    warning("NuPatch: umax={} exceeds uknot[nu]={}; ignoring.", umax, uknot[nu]);
    return false;
  }
  if (!nuPatchRangeNonEmpty(vmin, vmax)) {
    warning("NuPatch: vmin={} vmax={} violates vmin < vmax; ignoring.", vmin, vmax);
    return false;
  }
  if (!nuPatchRangeAboveFloor(vmin, vknot, vorder)) {
    warning("NuPatch: vmin={} is less than vknot[vorder-1]={}; ignoring.", vmin, vknot[vorder - 1]);
    return false;
  }
  if (!nuPatchRangeBelowCeiling(vmax, vknot, nv)) {
    warning("NuPatch: vmax={} exceeds vknot[nv]={}; ignoring.", vmax, vknot[nv]);
    return false;
  }
  return true;
}

} // namespace

RtVoid GMANRenderManImpl::RiNuPatchV(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin, RtFloat umax, RtInt nv,
                                     RtInt vorder, RtFloat vknot[], RtFloat vmin, RtFloat vmax, RtInt n,
                                     RtToken tokens[], RtPointer parms[]) {
  RiNuPatchV(nu, uorder, uknot, umin, umax, nv, vorder, vknot, vmin, vmax, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiNuPatchV(RtInt nu, RtInt uorder, RtFloat uknot[], RtFloat umin, RtFloat umax, RtInt nv,
                                     RtInt vorder, RtFloat vknot[], RtFloat vmin, RtFloat vmax, RtInt n,
                                     RtToken tokens[], RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdNuPatch)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }

  if (!validateNuPatch(nu, uorder, uknot, umin, umax, nv, vorder, vknot, vmin, vmax)) {
    worldManager->add(objectManager->create());
    return;
  }

  // RiSpec's NuPatch sizing: vertex nu*nv; with nusegments = nu-uorder+1
  // and nvsegments = nv-vorder+1, varying (nusegments+1)*(nvsegments+1),
  // uniform nusegments*nvsegments, facevarying 1.
  const std::int64_t nusegments = std::int64_t{nu} - uorder + 1;
  const std::int64_t nvsegments = std::int64_t{nv} - vorder + 1;
  const RtInt vertex = saturatedCount(std::int64_t{nu} * nv);
  const RtInt varying = saturatedCount((nusegments + 1) * (nvsegments + 1));
  const RtInt uniform = saturatedCount(nusegments * nvsegments);

  GMANParameterList paramList(dictionary, n, tokens, parms, vertex, varying, uniform, 1, counts);

  // A non-positive "Pw" weight is not itself an RISpec rule; it closes the
  // 0/0 a zero weight would otherwise hand the evaluator's rational
  // division. "Pw" exists only when the parameter list accepted its
  // vertex*4 size, a shorter supplied "Pw" having stopped the file, so this
  // loop is memory-safe.
  RtFloat* pw = (RtFloat*)paramList.getPointer(dictionary.getTokenId(RI_PW));
  if (pw) {
    for (RtInt i = 0; i < vertex; i++) {
      RtFloat w = pw[4 * i + 3];
      if (w <= 0) {
        warning("NuPatch: control point {} has non-positive \"Pw\" weight "
                "{}; ignoring.",
                i, w);
        worldManager->add(objectManager->create());
        return;
      }
    }
  }

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSNuPatch(nu, uorder, uknot, umin, umax, nv, vorder, vknot, vmin, vmax, paramList,
                                     &(getOptions()), &(getAttributes()), &transform);
  worldManager->add(prim);
}

RtVoid GMANRenderManImpl::RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                                    RtPointer parms[]) {
  RiSphereV(radius, zmin, zmax, tmax, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiSphereV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n, RtToken tokens[],
                                    RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdSphere)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4, 1, 1, counts);

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim =
      objectManager->getRSSphere(radius, zmin, zmax, tmax, paramList, &(getOptions()), &(getAttributes()), &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiConeV(RtFloat height, RtFloat radius, RtFloat tmax, RtInt n, RtToken tokens[],
                                  RtPointer parms[]) {
  RiConeV(height, radius, tmax, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiConeV(RtFloat height, RtFloat radius, RtFloat tmax, RtInt n, RtToken tokens[],
                                  RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdCone)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4, 1, 1, counts);

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSCone(height, radius, tmax, paramList, &(getOptions()), &(getAttributes()), &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n,
                                      RtToken tokens[], RtPointer parms[]) {
  RiCylinderV(radius, zmin, zmax, tmax, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiCylinderV(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n,
                                      RtToken tokens[], RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdCylinder)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4, 1, 1, counts);

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSCylinder(radius, zmin, zmax, tmax, paramList, &(getOptions()), &(getAttributes()),
                                      &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax, RtInt n, RtToken tokens[],
                                         RtPointer parms[]) {
  RiHyperboloidV(point1, point2, tmax, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiHyperboloidV(RtPoint point1, RtPoint point2, RtFloat tmax, RtInt n, RtToken tokens[],
                                         RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdHyperboloid)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4, 1, 1, counts);

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim =
      objectManager->getRSHyperboloid(point1, point2, tmax, paramList, &(getOptions()), &(getAttributes()), &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n,
                                        RtToken tokens[], RtPointer parms[]) {
  RiParaboloidV(rmax, zmin, zmax, tmax, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiParaboloidV(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax, RtInt n,
                                        RtToken tokens[], RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdParaboloid)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4, 1, 1, counts);

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSParaboloid(rmax, zmin, zmax, tmax, paramList, &(getOptions()), &(getAttributes()),
                                        &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax, RtInt n, RtToken tokens[],
                                  RtPointer parms[]) {
  RiDiskV(height, radius, tmax, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiDiskV(RtFloat height, RtFloat radius, RtFloat tmax, RtInt n, RtToken tokens[],
                                  RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdDisk)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4, 1, 1, counts);

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSDisk(height, radius, tmax, paramList, &(getOptions()), &(getAttributes()), &transform);
  worldManager->add(prim);
}
RtVoid GMANRenderManImpl::RiTorusV(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax, RtFloat tmax,
                                   RtInt n, RtToken tokens[], RtPointer parms[]) {
  RiTorusV(majrad, minrad, phimin, phimax, tmax, n, tokens, parms, NULL);
}
RtVoid GMANRenderManImpl::RiTorusV(RtFloat majrad, RtFloat minrad, RtFloat phimin, RtFloat phimax, RtFloat tmax,
                                   RtInt n, RtToken tokens[], RtPointer parms[], RtInt const* counts) {
  if (!allowed(cmdTorus)) {
    return;
  }
  if (!renderer) {
    reportNoRendererOnce();
    return;
  }
  GMANParameterList paramList(dictionary, n, tokens, parms, 4, 4, 1, 1, counts);

  GMANTransform transform(getTransform());
  GMANPrimitive* prim;

  prim = objectManager->getRSTorus(majrad, minrad, phimin, phimax, tmax, paramList, &(getOptions()), &(getAttributes()),
                                   &transform);
  worldManager->add(prim);
}

RtVoid GMANRenderManImpl::RiBlobbyV(RtInt /*nleaf*/, RtInt /*ncode*/, RtInt /*code*/[], RtInt /*nflt*/,
                                    RtFloat /*flt*/[], RtInt /*nstr*/, RtToken /*str*/[], RtInt /*n*/,
                                    RtToken /*tokens*/[], RtPointer /*parms*/[]) {
  if (!allowed(cmdBlobby)) {
    return;
  }
}
RtVoid GMANRenderManImpl::RiPointsV(RtInt /*npoints*/, RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[]) {
  if (!allowed(cmdPoints)) {
    return;
  }
}
RtVoid GMANRenderManImpl::RiCurvesV(RtToken /*type*/, RtInt /*ncurves*/, RtInt /*nvertices*/[], RtToken /*wrap*/,
                                    RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[]) {
  if (!allowed(cmdCurves)) {
    return;
  }
}
RtVoid GMANRenderManImpl::RiSubdivisionMeshV(RtToken /*mask*/, RtInt /*nf*/, RtInt /*nverts*/[], RtInt /*verts*/[],
                                             RtInt /*ntags*/, RtToken /*tags*/[], RtInt /*numargs*/[],
                                             RtInt /*intargs*/[], RtFloat /*floatargs*/[], RtInt /*n*/,
                                             RtToken /*tokens*/[], RtPointer /*parms*/[]) {
  if (!allowed(cmdSubdivisionMesh)) {
    return;
  }
}

RtVoid GMANRenderManImpl::RiProcedural(RtPointer /*data*/, RtBound /*bound*/,
                                       RtVoid (* /*subdivfunc*/)(RtPointer, RtFloat),
                                       RtVoid (* /*freefunc*/)(RtPointer)) {
  if (!allowed(cmdProcedural)) {
    return;
  }
}
RtVoid GMANRenderManImpl::RiGeometryV(RtToken /*type*/, RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[]) {
  if (!allowed(cmdGeometry)) {
    return;
  }
}

// ****************************************************
// ******* ******* ******* MISC ******* ******* *******
// ****************************************************
RtVoid GMANRenderManImpl::RiMakeTextureV(char* pic, char* tex, RtToken swrap, RtToken twrap,
                                         RtFilterFunc /*filterfunc*/, RtFloat /*swidth*/, RtFloat /*twidth*/,
                                         RtInt /*n*/, RtToken /*tokens*/[], RtPointer /*parms*/[]) {
  if (!allowed(cmdMakeTexture)) {
    return;
  }
  gman::makeTexture(pic, tex, swrap, twrap);
}
RtVoid GMANRenderManImpl::RiMakeBumpV(char* /*pic*/, char* /*tex*/, RtToken /*swrap*/, RtToken /*twrap*/,
                                      RtFilterFunc /*filterfunc*/, RtFloat /*swidth*/, RtFloat /*twidth*/, RtInt /*n*/,
                                      RtToken /*tokens*/[], RtPointer /*parms*/[]) {
  if (!allowed(cmdMakeBump)) {
    return;
  }
}
RtVoid GMANRenderManImpl::RiMakeLatLongEnvironmentV(char* pic, char* tex, RtFilterFunc /*filterfunc*/,
                                                    RtFloat /*swidth*/, RtFloat /*twidth*/, RtInt /*n*/,
                                                    RtToken /*tokens*/[], RtPointer /*parms*/[]) {
  if (!allowed(cmdMakeLatLongEnvironment)) {
    return;
  }
  gman::makeLatLongEnvironment(pic, tex);
}
RtVoid GMANRenderManImpl::RiMakeCubeFaceEnvironmentV(char* /*px*/, char* /*nx*/, char* /*py*/, char* /*ny*/,
                                                     char* /*pz*/, char* /*nz*/, char* /*tex*/, RtFloat /*fov*/,
                                                     RtFilterFunc /*filterfunc*/, RtFloat /*swidth*/,
                                                     RtFloat /*ywidth*/, RtInt /*n*/, RtToken /*tokens*/[],
                                                     RtPointer /*parms*/[]) {
  if (!allowed(cmdMakeCubeFaceEnvironment)) {
    return;
  }
}
RtVoid GMANRenderManImpl::RiMakeShadowV(char* /*pic*/, char* /*tex*/, RtInt /*n*/, RtToken /*tokens*/[],
                                        RtPointer /*parms*/[]) {
  if (!allowed(cmdMakeShadow)) {
    return;
  }
}

// ******************************************************************
// ******* ******* ******* ERROR HANDLER ******* ******* *******
// ******************************************************************

// Present on the interface and absent from this class until phase 0. It
// installs the handler GMANHandleError dispatches through.
RtVoid GMANRenderManImpl::RiErrorHandler(RtErrorHandler handler) { GMANErrorHandler = handler; }
