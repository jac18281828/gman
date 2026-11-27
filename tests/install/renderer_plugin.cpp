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
 * A renderer built outside the gman tree from the installed headers alone,
 * linking gman::gman and gman::gman_ray and nothing else. It subclasses
 * GMANRenderer, hands back a core world manager and a ray object manager,
 * calls one exported function of each library and exports the entry points a
 * renderer plugin defines. The module is built and linked only.
 */

#include <gman/gmanlinearworldmanager.h>
#include <gman/gmanloadable.h>
#include <gman/gmanlog.h>
#include <gman/gmanrayoccluder.h>
#include <gman/gmanrayprimitivefactory.h>
#include <gman/gmanrenderer.h>

namespace {

class InvertingRenderer : public GMANRenderer {
public:
  RtVoid illuminance(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) override {}
  RtVoid illuminate(RtInt /*i*/, GMANPoint const& /*p*/, GMANVector const& /*axis*/, RtFloat /*angle*/) override {}
  RtVoid solar(RtInt /*i*/, GMANVector const& /*axis*/, RtFloat /*angle*/) override {}

  RtFloat getDepth(int /*xs*/, int /*ys*/) const override { return RI_INFINITY; }

  RtVoid render(GMANFrameBuffer* frameBuffer, GMANViewingSystem* /*viewingSys*/, GMANOptions const& /*options*/,
                GMANAttributes const& /*attributes*/) override {
    frameBuffer->setPixel(0, 0, gman::oneMinus(frameBuffer->getPixel(0, 0)));
    logWrite(LOGLVL_DEBUG, "inverted the first pixel");
  }

  GMANWorldManager* getWorldManager(RtVoid) override { return &worldManager; }
  GMANPrimitiveFactory* getPrimitiveFactory(RtVoid) override { return &primitiveFactory; }

private:
  GMANLinearWorldManager worldManager;
  GMANRayPrimitiveFactory primitiveFactory;
};

GMANLoadableObjectInfo loadableInfo = {
    "Inverting renderer",
    "install test",
    "A renderer built against an installed gman.",
};

InvertingRenderer renderer;

} // namespace

extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANRenderer* GMANLoadRenderer(void) { return &renderer; }
