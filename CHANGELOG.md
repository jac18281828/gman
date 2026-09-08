0.7.1 (2026-09-08)

* **A malformed RIB could make the renderer read past the end of a heap allocation.** `Patch "bilinear" "P" [-1 -1 0  1 -1 0]` declares four vertices — twelve floats — and supplies six. `GMANParameterList` sized its destination from the declared class and copied that many elements from the supplied array, running off the end of the parser's buffer and rendering whatever happened to be next in memory as vertex coordinates. Short arrays are now clamped and zero-filled, with a warning naming the parameter and both lengths. All three copy paths are covered — float, integer and string — since each carried the identical unbounded loop
* **This affected 0.6.0 and 0.7.0.** The defect predates `PatchMesh`, where review found it: the trace above is a plain `Patch`. Anyone running an earlier release should upgrade. A renderer's whole job is parsing files it did not write, which makes an out-of-bounds read on malformed input a defect in the primary input path rather than an edge case
* **The parser already knew the length and threw it away.** `parseParameterList` recorded each array's element count and discarded it. The `Ri*V` entry points cannot carry it — those signatures are fixed by the RenderMan interface — so the count now travels beside the call rather than through it. Supplying no count still means "trust the caller", which keeps the public C API's contract intact: a program calling `RiPatchV` directly owns that memory, while a RIB file is untrusted
* **A zero basis step no longer divides by zero.** `validBicubicMeshDim`'s nonperiodic branch computed `(n - 4) % step` with no guard on `step`, where its periodic sibling rejected `step < 1` outright. Unreachable through RIB, which rejects a bad step earlier, and reachable through `GMANPatchPolyObjectManager` directly
* **The memcheck leg tests more than two files.** Valgrind in CI ran `sphere.rib` and `corpus/menger.rib` and nothing else, so no `PatchMesh` fixture and no malformed input ever reached a sanitizer there. That is why the overflow above survived review and a merge. A periodic bicubic `PatchMesh` now runs under memcheck

0.7.0 (2026-09-07)

* **Anti-aliasing.** `PixelSamples` and `PixelFilter` were parsed, stored and discarded; every render was one sample per pixel whatever the scene asked for. A per-sample buffer now sits behind the z-buffer renderer, and the pixel filter convolves over its support rather than the renderer averaging within a pixel. Every image the renderer produces changes. Samples land at `(j+0.5)/n` within a pixel — a truncating placement would have shifted every image a fraction of a pixel, which no surviving test would have caught
* **`Patch` renders, bilinear and bicubic.** It had parsed and drawn nothing since 1999. Landing it exposed the defect that had kept it dark: `GMANVector4::operator*=(const GMANMatrix4 &)` sliced itself to a 3D temporary, transformed that, and threw it away, so a projective transform of a 4-component point silently did nothing. The header had documented that exact bug shape as already fixed for its sibling overloads. Bicubic also needed its tangents normalized before crossing them, or the surface normal scaled with the parameterization
* **`PatchMesh` renders, bilinear and bicubic, periodic and non-periodic.** Includes B-spline, Bezier, Catmull-Rom and Hermite bases through `RiBasis`, and mixed `uwrap`/`vwrap`. Two arithmetic defects came out of it: the last sub-patch of a periodic mesh read one row past the grid instead of wrapping to the first, and a zero-step basis divided by zero before any control point was touched
* **Two of the five pixel filters were wrong, and nothing had ever called them.** `RiTriangleFilter` returned nonzero — sometimes negative — weights outside its support, so a filter that should vanish at its edge instead subtracted light beyond it. `RiGaussianFilter` was missing the RISpec's `2/width` rescale of its argument, leaving the kernel at 61% of peak at the support edge where the reference sits at 13.5%. Both were found by evaluating the functions rather than by reading them. Box, Catmull-Rom and sinc are pinned as-found by tests rather than "fixed"
* **The golden-image tolerance is measured rather than guessed.** It had accepted 24/255 per channel across up to 1% of pixels — values picked for one scene during the revival and adopted tree-wide. That window absorbed a real regression: reverting the perspective screen-window clip fix moved 732 of 120,000 pixels and the `shaders` golden still passed. Measuring the actual cross-platform spread found macOS matching every pixel exactly and Linux differing by at most six pixels in one scene, so the fraction tightened to 0.1% — about 180x headroom over the worst case observed. That same revert now turns `shaders` red
* **The test suite carries labels, timeouts and a real skip.** Every test declares `LABELS` and a `TIMEOUT`; the JPEG driver test skips rather than fails where libjpeg is absent. 34 tests, up from 31
* **Intel Mac binaries are gone from releases.** The `macos-13` leg of `0.6.0` sat queued for five hours without ever being assigned a runner and published six assets where eight were expected. The matrix is now the three legs that actually get runners: linux-x86_64, linux-arm64, macos-arm64. Building on an Intel Mac is still supported; nobody ships you a binary for it

**Known gaps.** No texturing — `texture()`, `environment()`, `shadow()` and `bump()` do not exist, though `s`/`t` reach the shader. No shadows: a light illuminates through geometry. No `spotlight`. `NuPatch`, curves and points parse but do not rasterize. Single-threaded — the 1999 pthread wrapper is still present, still uncalled, and two of its methods are empty bodies. The REYES, ray-tracing and radiosity plugins remain non-functional and built `OFF`. Shaders are C++ plugins; the shading-language compiler was never finished.

0.6.0 (2026-09-06)

* **The first release in 24 years.** GMAN was written between 1999 and 2002, shelved at 0.1.0, and resumed in 2026. Everything below is the revival arc; the entries under it are the original history, kept verbatim from the `ChangeLog` this file replaces
* **It renders.** All seven RenderMan quadrics — `Sphere`, `Cone`, `Cylinder`, `Hyperboloid`, `Paraboloid`, `Torus`, `Disk` — plus convex `Polygon`, shaded by matte, plastic or metal, lit by `ambientlight`, `distantlight` and `pointlight`, written out as TIFF, PNG, JPEG or PNM. At the start of the revival `gman sphere.rib` exited 1 with `TOKEN_NOT_FOUND` and produced no file
* **Builds with CMake 3.21 presets and C++17.** Autotools is gone, along with the Windows tree and the `GMANDLL` macro (now `GMAN_EXPORT`, across 95 files). Three presets: `dev` is RelWithDebInfo, `debug` adds ASan and UBSan, `release` is Release
* **The coordinate chain works end to end.** Object → world → camera → screen → NDC → raster. `GMANMatrix4::prjPersp` was rewritten; `RiWorldBegin` had shadowed the field of view with its default, and the output driver leaked on every frame
* **Array parameters reached the renderer for the first time.** `fov [45]` parsed, then was silently dropped, so every scene rendered at the default field of view whatever it asked for. This defect is why the revival ran phases 0 → 2 → 1 rather than in order
* **Transform composition order was backwards.** `buildTransform` accumulated `CTM_old · Local` for a row-vector chain that requires `Local · CTM_old`, so an in-world `Rotate` orbited its primitive around the camera instead of the world origin — a sphere's silhouette centroid moved 99 → 192 px across 0 → 30° of rotation and left the frame entirely by 45°. Invisible to the whole test suite at the time, because every rendering fixture used only `Translate`
* **Perspective clipping honors the screen window.** `GMANPolygonClipper::clip` tested perspective geometry against fixed ±1 side planes while computing the correct planes for orthographic. At 640×400 the default window is x ∈ [-1.6, 1.6], so only columns 120–520 of 640 ever drew and 37% of every wide frame was background
* **Convex `Polygon` rasterizes.** `GMANPolygon` had been declared and wired to nothing. Landing it exposed two more defects it made observable: `RiPolygonV` sized its parameters wrongly, and `GMANRIBParse::parsePolygon` hardcoded a vertex count of zero
* **Primitives carry normals, and shading runs.** `calcNormal` is wired, backface culling is real rather than assumed, lights illuminate, and C++ surface shaders execute against a populated shading environment
* **Loading two shaders in one scene no longer aborts.** `GMANLoadableShader` let its `dlopen` handle refcount reach zero at `AttributeEnd` and closed the module while a later `AttributeBegin` still referenced the address the loader recycled. Handles are now retained for the life of the process
* **`GMANQuantize::doColor` stopped logging once per pixel.** A 640×400 render emitted 256,000 identical warnings; it now warns once
* **The RIB front end covers a working subset of RISpec 3.2.** Camera and transform setup, lights, surface shaders, array and inline-declared parameters, `facevarying` parameters, gzip'd streams and `ReadArchive` with cycle detection. Any request GMAN does not implement warns once, skips and keeps parsing, so an unsupported scene degrades instead of aborting — 23 requests take that path deliberately. Aqsis' `menger.rib` parses end to end
* **A real test suite and CI.** Golden-image regression over a checked-in RIB corpus, unit tests for the matrix and space chain, and seven CI legs: macOS and Linux against both gcc and clang, an ASan/UBSan build, valgrind memcheck, and a clang-format check. The Linux legs caught what macOS clang cannot see — 26 `delete` on `new[]`, an incompatible function type, ignored-qualifier casts and a 1,332-byte-per-context leak
* **The license position is settled.** LGPL-2.1-or-later with SPDX identifiers across 220 files; the pre-2005 FSF address is corrected and "GNU GMAN Library" is retired — GMAN was never an accepted GNU package. The name is now "GMAN, a RenderMan-compatible renderer", which drops a trademark use that was the project's only real legal exposure
* **Documentation describes the renderer that exists.** The README is markdown and leads with what GMAN does; `CONTRIBUTING.md` replaces a coding guide written in 2001; `AGENTS.md` covers the conventions and traps for an AI agent working the tree; the 1999 LaTeX manual no longer cites headers that never existed or a compiler from 1998. `INSTALL`, `disclaimer.txt` and the old `ChangeLog` are deleted

**Known gaps.** No texturing. No anti-aliasing — one sample per pixel, though `PixelSamples` and `PixelFilter` parse and five filter kernels are implemented. `Patch`, `PatchMesh`, `NuPatch`, curves and points parse but do not rasterize. The REYES, ray-tracing and radiosity renderer plugins are present, non-functional and built `OFF` by default. Shaders are C++ plugins; the shading-language compiler was never finished and is kept only as a record.

0.1.0 (2000-02-13)

* First major rewrite, and code restructure
* Improved object oriented model and design

0.0005 (1999-11-15)

* Added GNU Autoconf/Automake support

0.0004 (1999-10-28)

* Added the gman.1 manual page

0.0003 (1999-05-08)

* Added some more code, and improved the zbuffer and base shaders

0.0002 (1999-04-17)

* Added automake/autoconf and rip parse objects

0.0001 (1999-04-10)

* Added support for Points, Vectors, and Matricies and other base environment objects

0.0000 (1999-03-01)

* Started this crazy project
