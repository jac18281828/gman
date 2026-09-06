0.6.0 (2026-09-06)

* **The first release in 24 years.** GMAN was written between 1999 and 2002, shelved at 0.1.0, and resumed in 2026. Everything below is the revival arc; the entries under it are the original history, kept verbatim from the `ChangeLog` this file replaces
* **It renders.** All seven RenderMan quadrics — `Sphere`, `Cone`, `Cylinder`, `Hyperboloid`, `Paraboloid`, `Torus`, `Disk` — plus convex `Polygon`, shaded by matte, plastic or metal, lit by `ambientlight`, `distantlight` and `pointlight`, written out as TIFF, PNG, JPEG or PNM. At the start of the revival `gman sphere.rib` exited 1 with `TOKEN_NOT_FOUND` and produced no file
* **Builds with CMake 3.25 presets and C++23.** Autotools is gone, along with the Windows tree and the `GMANDLL` macro (now `GMAN_EXPORT`, across 95 files). Three presets: `dev` is RelWithDebInfo, `debug` adds ASan and UBSan, `release` is Release
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
