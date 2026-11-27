# Changelog

Each release opens with its cost to upgrade. **Breaking** means you rebuild, relink or edit a scene or script, or that existing scenes render differently by design; **Incremental** means the release drops in. Breaking changes, new features and fixes follow.

1.4.0 (2026-11-26)

**Breaking.** Rebuild every plugin and program that links gman, switch to the renamed CMake targets and check scripts against the new command line; ray-traced scenes with area lights, glass or opacity-mapped cutouts render differently.

**Breaking changes**

* The binary interface changes: `GMANOptions` gains `isColorQuantizeSet`, `gman::Lobe` gains `transmitWeight` and `GMANRayOccluder`'s constructor takes a `TextureCache&`
* The core library is `libgman`, CMake target `gman::gman`, where it was `libgman_core`; the executable's CMake target is `gman_cli`, and the program is still `gman`
* A usage error prints on stderr and exits with CLI11's status, such as 109 for an unknown flag, where 1.3 exited 1
* A flag after a file applies, so `gman scene.rib -q` renders quietly; `--` ends the options
* A run needs a file, so `gman -d` alone is a usage error
* `GMANQuadraticRoots` is gone from `gmanmath.h`
* Under the ray tracer, a shadow passes through glass and opacity-mapped cutouts, where glass and a cutout card cast a solid shadow
* The ray tracer lights from area lights; `Option "raytracer" "integer arealightsamples"` sets the draws per light (default 4), and area-lit scenes render slower

**New**

* Every flag has a long name, and help prints on stdout and exits 0
* `AreaLightSource` on `Polygon`, `GeneralPolygon`, `PointsPolygons` and `PointsGeneralPolygons` emits under both tracers, one-sided along the winding's normal; patch emitters work but are slow
* The ray tracer renders `Patch` and `PatchMesh`
* `glass` reads `eta`, `Kr` and `Kt`, defaulting to BMRT's 1.5, 1 and 1; `eta` and `Kt` need an inline declaration, as in `"float eta" [1.33]`
* `gman::BSDF` adds `addDielectric(reflectWeight, transmitWeight, eta)`
* `Option "pathtracer" "string denoise" ["atrous"]` denoises a path-traced image with an edge-avoiding à-trous filter
* A build with `GMAN_WITH_EXR` writes linear float OpenEXR. gman still reads sRGB textures and colours as linear, so EXR output looks washed out in a colour-managed viewer until colour management lands (#12)
* The release tarball's binaries are stripped: `gman` is 412 kB, down from 519 kB

**Fixed**

* A surface near an area light collects no white specks under either tracer
* Under the ray tracer, a partly opaque surface seen through glass or in a mirror composites as it does in direct view
* A texture on a periodic mesh keeps its direction across the seam
* A non-finite request value, such as a `Format` aspect, `FrameAspectRatio`, `PixelFilter` width or `Exposure`, is ignored
* A NaN shader colour reads as 0 and a NaN coverage as opaque
* `noise` and texture lookups return NaN for a non-finite coordinate
* An area emitter with a singular placement contributes nothing
* The C API frees a render context whose `RiBegin` fails

1.3.1 (2026-11-24)

**Incremental.** The macOS arm64 tarball 1.3.0 lacked ships, and no library code changes.

1.3.0 (2026-11-24)

**Breaking.** Rebuild every plugin and program that links gman. PNG alpha changes meaning, and malformed files that rendered before now stop.

**Breaking changes**

* The binary interface changes; `SameMinorVersion` refuses 1.3 to a consumer asking for 1.2
* PNG writes straight alpha, so a tool that divides gman PNG colour by alpha must stop; TIFF stays premultiplied
* The RIB parser stops the file on an array that is short, mistyped or at odds with its count
* A `Quantize` the output driver cannot store stops the file
* Imagers run, so a scene's `Imager` request, ignored before, changes its image
* `GMANMAX`, `GMANMIN` and `PI` are gone, and `GMANMin`, `GMANMax` and `GMANClamp` drop a NaN operand

**New**

* The ray tracer and the radiosity pass run on every core, and the BVH builds by binned surface area: the vase ray-traces 9× faster than in 1.2.0 and the bike scene 23× faster
* gman ships the standard `background` imager
* `paintedplastic` takes an opacity map and a tangent-space normal map
* `PixelVariance` stops a path-traced pixel early without biasing it
* RIB `ColorSamples` sets the colour sample count, and `Color` and `Opacity` read at it
* `gmanshaderparams.h` installs, so a shader reads its declared parameters by name
* Ray-traced shading points carry `dPdu` and `dPdv`
* The release preset links with LTO

**Fixed**

* A request issued out of mode warns and is ignored, and the file renders on
* A shader that fails to load reports `<Request> "<name>" failed to load: <reason>` and stops the file
* A missing texture warns once per world instead of once per worker
* A polygon's per-vertex `"N"` shades smoothly under all three renderers, and its shadows lose the dark wedges along triangle edges
* Shading splines evaluate the basis they name
* The ray kernel returns no NaN

1.2.0 (2026-11-17)

**Breaking.** Relink any program or plugin that uses ray or raster code, and rebuild any that touches `GMANViewingSystem` or `GMANOptions`. Images are unchanged.

**Breaking changes**

* gman splits into three shared libraries. `libgman_core` keeps the RenderMan interface, its state and the shared contracts; the ray geometry, BVH, occluder, emitters and indirect-light pass move to `libgman_ray`, and the z-buffer's raster sources to `libgman_raster`. A consumer that uses a moved symbol links `gman::gman_ray` or `gman::gman_raster`, and `SameMinorVersion` refuses 1.2 to a consumer asking for 1.1
* `gmanribparse.h`, `gmanribtokenize.h`, `gmanrendermanimpl.h` and `gmangraphicstate.h` stop installing; `GMANRIBParse` and `GMANRenderManImpl` stay exported
* `GMANViewingSystem::cameraRay`, `ray`, `rasterToScreen` and `screenToRaster` are `const`, so an override of `cameraRay` adds `const`
* `GMANViewingSystem::visible(GMANFace)` gives way to the pure virtual `facesCamera(GMANPoint const&, GMANVector const&) const`, and `gman::faceVisible` in `gmanface.h` applies `Sides` and `Orientation`
* `GMANOptions` gains a trailing member, so code that constructs, copies or embeds one rebuilds

**New**

* The path tracer renders on every core, and an image is byte-identical at any worker count
* Ray headers export through `GMANRAY_EXPORT` from `<gman/rayexport.h>`, and raster headers through `GMANRASTER_EXPORT`
* `gmanparallel.h` installs `gman::parallelFor` and `gman::parallelWorkers`, which a renderer plugin shards its work through
* `gmanviewingsysteminputs.h` installs, and `gman::makeViewingSystem` builds the viewing system from `gman::resolveViewingSystemInputs`
* `GMANOptions::getNamedOption` reads any `Option` parameter the typed handlers do not, by option and parameter name, so a renderer reads its own options without a new handler
* Every installed library and plugin carries the install run path, so a plugin finds the library it names
* `samples/vase.rib` renders at 1280 by 800 with a 16-bit tiled floor and water from `samples/vasewater.rib`, and `samples/materials-pathtraced.png` is a 2048-path render

**Fixed**

* A parameter whose value contradicts its declaration is dropped with one error naming the request and the token, and the file renders on; `Option "user" "string mode" [1]` crashed the renderer, a short array was over-read and a large declared count overflowed. A mixed array, or a float literal for a declared integer, still stops the file

1.1.0 (2026-11-16)

**Breaking.** Rebuild an output driver built outside the tree; a scene renders differently only where it reads a 16-bit texture or asks TIFF for float samples.

**Breaking changes**

* `GMANOutput` gains `writesFloat()` and `writeFloatImage(DisplayMode, std::vector<float> const&, RtFloat)`, moving its vtable; `SameMinorVersion` refuses 1.1 to a consumer asking for 1.0
* `gmansegment.h` and `GMANSegment` are gone, and `gmantypes.h` stops declaring `GMANFace`, `GMANSurface`, `GMANFacePtr`, `GMANSurfacePtr`, `GMANFaceList` and `GMANSurfaceList`
* `Quantize "rgba" 0 0 0 0` with a TIFF `Display` writes 32-bit float samples after gain and gamma, unclamped, so radiance above 1 reaches the file, where it wrote 8 bits with a warning; other drivers keep that fallback

**New**

* `MakeTexture` and `MakeLatLongEnvironment` write a 16-bit texture from a 16-bit picture

**Fixed**

* A 16-bit TIFF texture samples at 16-bit precision. A 16-bit layout read only at 8 bits, such as a tiled or bottom-up one, renders and warns, naming the file

1.0.2 (2026-10-18)

**Incremental.** Shared polygon edges and quadric rims render without seams under the ray tracer and the radiosity pass.

**Fixed**

* A ray landing on an edge two polygons share hits, so the ray tracer leaves no transparent hairline there and the radiosity pass closes its corner seams
* The radiosity pass leaves no black arc on a quadric's rim. It now chooses an element's side from the hit's normal, so a quadric shades differently over its whole body: a torus the old pass painted flat grey shades like a cylinder

1.0.1 (2026-10-09)

**Incremental.** The z-buffer draws no spikes at the frame edge.

**New**

* `samples/materials.rib` sets gold, glass and mirror balls and a plastic torus before a clay wall under two area lights; `samples/materials-pathtraced.png` is its render, and the release tarball carries the scene

**Fixed**

* A polygon leaving the frame blends its clipped vertex's colour and alpha along the edge, so the z-buffer draws no spikes at the frame edge

1.0.0 (2026-09-29)

**Breaking.** Rebuild every shader, renderer and output-driver plugin. `gman` stops a file at its first error, and output changes in every format.

**Breaking changes**

* The plugin binary interface changes, and every shader, renderer and output-driver plugin rebuilds:
  * `GMANLoadShader` takes `GMANParameterList const&` and builds one shader instance per `Surface` call, and `GMANDestroyShader(GMANShader*)` is required; both are `extern "C" GMAN_EXPORT` in `gmanshader.h`
  * `computeCi` and `computeOi` are `const` and return colour by value, so a shader drops mutable state and function-local statics
  * `GMANSurfaceShader` gains the virtuals `bsdf` and `albedo`; a `bsdf` override must not call the base `albedo`, since the two would recurse
  * `GMANShader::set` and the protected `pl` and `renderer` members are gone, and `GMANLoadableShader` takes `(char const*, GMANParameterList const&)` and is neither copyable nor movable
  * The `GMANAttributes` shader setters and `GMANOptions::setImager` lose their `GMANRenderer&` parameter, `getSurface` returns `std::shared_ptr<GMANSurfaceShader const>`, `getSurfaceParameters` is gone and `GMANOptions::imagerModule` is a `std::shared_ptr<GMANLoadableShader>`
  * `GMANSurfaceEnv` gains its texture cache and a trailing `indirect` colour, which `ambient()` adds, and `gman::shade` takes `TextureCache*` and `GMANColor const* indirect`
  * `GMANObjectManager`'s four polygon virtuals give way to the pure virtual `getRSPolygonMesh` over `GMANPolygonMesh`
  * `GMANQuantize` is an aggregate of `one`, `min`, `max` and `ditheramplitude`
  * An output driver's `writeImage` receives packed samples and their bit depth, and `GMANColorRGB` is gone
  * The column-vector `GMANPoint::operator*(GMANMatrix4 const&)` and `GMANVector::operator*(GMANMatrix4 const&)` are gone; `gman::transformPoint` and `gman::transformDirection` are the row-vector forms
  * `gmanslapi.h` drops `distance`, `ptlined` and `rotate`, which had no definition
* `gman` stops a file at any error reported at `RIE_ERROR` or above: no image, exit status 1 and the run moves to the next file. A scene naming an undeclared token, such as `Surface "plastic" "Kdd" [0.2]`, rendered and exited 0 in 0.9.1. A stop inside `WorldBegin` makes every later file on the command line fail with `RIE_ILLSTATE`. An embedder keeps `RiErrorPrint` as its default handler and sees no change
* A `Surface` that fails to load reports `RIE_NOSHADER` at `RIE_ERROR`, names the request and binds the default surface
* Through the C API, `RiDisplacement`, `RiAtmosphere`, `RiInterior`, `RiExterior` and `RiImager` map a token to `lib<name>.so` as `Surface` does, so a caller that passed `"libmatte.so"` now fails; a failed load reports `RIE_NOSHADER` and leaves that shader unset
* `gman -r gmanradiosity` is gone; radiosity is a pass of the ray tracer, loaded as `libradiosity.so`
* `gman` rejects an unrecognized flag: `gman -x scene.rib` prints the usage text and exits 1
* Every output driver rounds and dithers by `Quantize`, so most existing renders change
* The framebuffer keeps unclamped colour until output, so `Exposure` gain past 1 clamps a channel to 255 where it wrapped, and a NaN writes 0
* A PNG honours its `DisplayMode`: `"rgb"` writes no alpha, `"a"` writes grey coverage and the other modes write premultiplied colour with real coverage alpha. PNG carries no `gAMA` chunk, so viewers show its bytes as written
* `noise()`, `pnoise()` and `cellnoise()` draw from their own seeded `std::mt19937`, so their values change and now agree across platforms, and the process's `rand()` sequence is left alone
* `build.sh` is gone; the `dev` preset and the devcontainer carry the same toolchain

**New**

* Path tracing: `gman -r gmanpathtracer scene.rib` traces unidirectional paths with next-event estimation and Russian roulette, and `Option "pathtracer" "integer samples" [n]` sets the paths per pixel
* Area lights: `AreaLightSource "arealight"` turns the `Sphere` or `Disk` after it into a one-sided emitter of `intensity` × `lightcolor`, which the path tracer samples with multiple importance sampling
* Bounce light under the ray tracer: `Option "render" "string indirect" ["radiosity"]` runs a radiosity pass, and `Option "radiosity" "float elementsize"` sets its patch size
* Every built-in shader returns a BSDF the path tracer samples: GGX beside diffuse for `plastic`, `paintedplastic`, `metal` and `shinymetal`, and exact-Fresnel delta lobes for `mirror` and `glass`
* A TIFF `Display` writes 16 bits a sample once `Quantize`'s `max` exceeds 255
* `gmansampling.h` installs a counter-based sampler with stratified patterns and the warps the path tracer draws with, and `Sphere` and `Disk` sample their own surface by area
* `gmantexture.h` installs `gman::TextureCache`; `gman::IndirectPass`, `gman::loadIndirectPass`, `gman::hitSurfacePoint`, `gman::albedo` and `gman::resolveLoadableShader` are exported
* A release tarball installs itself: `sudo ./install.sh`, with `--prefix DIR` and `--uninstall`

**Fixed**

* The C API's 38 variadic entry points walk their `va_list` once; on x86-64 they read past the caller's arguments
* The PNM, PNG and JPEG drivers raise `RIE_SYSTEM` on a failed write instead of exiting 0 with a truncated file, as does a failed TIFF scanline, though a full disk on a single-strip TIFF still exits 0; JPEG warns when its display mode asks for alpha or depth it cannot carry
* `RiPolygon` warns on a negative or overflowing `nverts` and adds nothing, where it aborted
* A RIB primitive parameter longer than its declared length warns before truncating
* `GMANMatrix4`'s scalar multiply scales all sixteen entries, so a moving transform interpolates correctly
* RIB `Points` passes its point count to `RiPointsV`; `Points` still draws nothing
* A RIB byte at or above 0x80 classifies correctly, and a primitive request that throws frees its transform

0.9.1 (2026-09-24)

**Incremental.** The ray tracer draws every polygon request, and the ray tracing headers install for plugins.

**New**

* The ray tracer draws `GeneralPolygon`, `PointsPolygons` and `PointsGeneralPolygons`, holes included
* The ray primitives, `GMANRayObjectManager`, `GMANRayBVH` and `GMANRayOccluder` install their headers, so a plugin can trace with them

**Fixed**

* The z-buffer dices a polygon by its size on screen: 10,000 small quads at 1600×900 render in 0.61 s and 288 MB, down from 3.53 s and 904 MB
* A redeclared `fov` takes effect

0.9.0 (2026-09-23)

**Breaking.** Rebuild every plugin and shader; existing scenes render differently.

**Breaking changes**

* The plugin binary interface changes, and every plugin and shader rebuilds. `gman::shade` takes a trailing `gman::Tracer const*`; `gman::Occluder::transmission` and `gman::Tracer::trace` take a trailing `RtFloat surfaceMagnitude`, which an implementation adds; `GMANSurfaceEnv`, `gman::SurfacePoint`, `GMANSampleBuffer`, `GMANOutVertex` and `GMANBitmap` change layout
* The default background is black, per the RISpec, where an uncovered pixel was white
* A TIFF `"rgba"` `Display` writes premultiplied coverage alpha, and `"rgb"` writes three samples
* A world block with no `Display`, or with a `"framebuffer"` one, raises `RIE_ILLSTATE` at `RiWorldEnd`
* The z-buffer dices a polygon before shading, so a texture reads correctly and a highlight resolves across the face, at about 31× the time and 21× the memory of 0.8.3 on 10,000 quads

**New**

* `mirror` and `glass` reflect and refract through the new `trace()` shading call under `-r gmanraytracer`; under the z-buffer `trace()` returns black. A shader that calls `trace()` reads every parameter before its first call and writes `Ci` after its last
* The ray tracer draws `Torus`, and a `Hyperboloid` whose two points share a z draws its annulus
* The ray tracer builds a bounding volume hierarchy each frame instead of testing every primitive
* `GMANAttributes`, `GMANWorldManager`, `GMANTorus`, `GMANPoint`, `GMANBBox`, `GMANColorSamples` and the light, volume, displacement and imager shader bases are exported, so a release-built plugin can subclass or copy them

**Fixed**

* A surface named more than once shades with its own `RiSurface` parameters, not the last call's
* `fresnel()` returns physical reflectance and transmittance; it returned NaN at normal incidence and values outside [0, 1] elsewhere
* A ray-traced quadric's hits land within 1e-5 of its surface at any distance
* A textured ray-traced `Polygon` interpolates its `s` and `t`
* `gman` survives a file with no `WorldBegin` after one that rendered, and `RiEnd` after a failed `RiBegin`

0.8.3 (2026-09-22)

**Breaking.** A RIB file with an out-of-range integer, or a float where an integer is declared, now fails, and two installed declarations change.

**Breaking changes**

* A float literal where an integer is declared raises `RIE_SYNTAX`, and an integer outside `RtInt` raises `RIE_RANGE`
* `gmancolor.h`'s `GMANConvert*` functions are gone, and `GMANToken::getLongInt()` is `getInt()`, returning `RtInt`

**New**

* The ray tracer renders, builds by default and installs: `gman -r gmanraytracer scene.rib` traces convex `Polygon` and every quadric but the torus, casts shadow rays and composites `Opacity` up to 16 layers. `-r <name>` loads any renderer plugin
* `gmanshading.h` installs `gman::shade`, `gman::Appearance` and `gman::SurfacePoint`, and `gmanocclude.h` installs `gman::Occluder`, the shading interface a renderer plugin calls

**Fixed**

* A parameter declared `integer` reaches its request as an `RtInt`, where `[4]` arrived as 1082130432
* A RIB string decodes its escapes, so an escaped quote stays in the string
* A short array on `Polygon` or a quadric zero-fills with a warning instead of reading past its allocation
* A `Declare` array size past `RtInt`'s range raises `RIE_RANGE` instead of aborting
* `GMANViewingSystem::cameraRay` honours the field of view

0.8.2 (2026-09-19)

**Breaking.** Rebuild every plugin; a release build exports only tagged symbols.

**Breaking changes**

* A release build exports only what `GMAN_EXPORT` tags, 793 symbols where 0.8.1 exported 1604, so a plugin calling an untagged symbol stops linking
* `RtBoolean` is a fixed `short`, changing the mangled name of every C++ signature that carries one; `RI_NULL` is the pointer-width `(RtToken)0`, which C++ must cast explicitly to a handle type such as `RtLightHandle`; `ri.h` drops `NCOMPS`
* 16 headers stop installing, `gmanparallel.h` among them
* `ri.h` spells `RiIlluminate` and `RiTransformEnd` correctly and drops the misspellings
* `GMANGammaCorrect` and `GMANOutput::setExposure` are gone; `gman::gammaCorrected` in `gmangamma.h` replaces them
* `gman_rib` is gone; `RiBegin` on a name ending `.rib` writes RIB through `libgmanrib.so`
* A textured sphere's texture turns half a turn, and a partial sphere starts its sweep at +x, as the RISpec and the other quadrics do
* Three diagnostics drop a `GMANContext:` or `GMANInlineParse:` prefix

**New**

* gman installs as a CMake package: `find_package(gman CONFIG REQUIRED)` and link `gman::gman_core`
* The tree's shaders live in `namespace gmanshader`, the convention for a new shader plugin

**Fixed**

* The install carries `libshinymetal.so`
* `Exposure` gain and gamma apply before a pixel narrows to a byte, so gamma lifts a channel below 1/255
* A C program that passes a shader a parameter survives its next `RiBegin`/`RiEnd` cycle and its exit

0.8.1 (2026-09-17)

**Incremental.** The release tarballs 0.8.0 lacked ship.

**Fixed**

* All three release tarballs ship; 0.8.0 shipped one

0.8.0 (2026-09-17)

**Breaking.** libtiff and C++20 are required, plugins drop their copyright field, and patches need reformatting.

**Breaking changes**

* libtiff is required, and `GMAN_WITH_TIFF` is gone
* C++20 is the floor: GCC 13 or Clang 17. `gmanguard.h`, `gmanmutex.h` and `gmanthread.h` are gone
* The PNG and JPEG drivers build only where their library is present, and their headers stop installing; a `Display` naming a driver the build lacks fails with `RIE_BADFILE`
* `GMANLoadableObjectInfo` drops its `copyright` field, and `GMANLoadable::getCopyright` and `GMANLog::copyright` are gone
* The tree is formatted with clang-format, so a patch against an earlier tree needs reformatting

**New**

* Texturing: `MakeTexture` makes a texture and `texture(name, s, t)` samples it, honouring `swrap` and `twrap`; polygons carry per-vertex `s` and `t`
* Environment maps: `MakeLatLongEnvironment` makes one, `environment(name, R)` samples it, and the `shinymetal` shader uses it
* `LightSource "spotlight"`, with `coneangle`, `conedeltaangle` and `beamdistribution` in radians as the RISpec specifies
* `GeneralPolygon`, `PointsPolygons` and `PointsGeneralPolygons` render, concave outlines and holes included
* `NuPatch` renders; `TrimCurve` parses and trims nothing yet
* `gman --version` lists the output drivers a build carries

**Fixed**

* `gman -l` stays inside its buffer and sets the log file without deadlocking
* A malformed RIB frees everything it allocated, and a `Declare` whose array size overflows is rejected
* The JPEG driver reports a file it cannot open, where it reported success

0.7.1 (2026-09-08)

**Incremental.** It fixes an out-of-bounds read on malformed RIB; upgrade from 0.6.0 or 0.7.0.

**Fixed**

* A RIB array shorter than its declaration zero-fills with a warning naming the parameter and both lengths, where it read past the parser's buffer: `Patch "bilinear" "P" [-1 -1 0  1 -1 0]` did. A program calling an `Ri*V` entry point directly still owns its array lengths
* A zero basis step in `GMANPatchPolyObjectManager` is rejected instead of dividing by zero

0.7.0 (2026-09-07)

**Breaking.** Every image changes: renders are anti-aliased.

**Breaking changes**

* `PixelSamples` and `PixelFilter` take effect, so every render changes
* Release binaries cover linux-x86_64, linux-arm64 and macos-arm64; Intel macOS builds from source

**New**

* The z-buffer anti-aliases through a per-sample buffer convolved by the pixel filter
* `Patch` renders, bilinear and bicubic
* `PatchMesh` renders, bilinear and bicubic, periodic or not, through the B-spline, Bezier, Catmull-Rom and Hermite bases

**Fixed**

* `RiTriangleFilter` is zero outside its support, and `RiGaussianFilter` applies the RISpec's `2/width` rescale
* `GMANVector4::operator*=(GMANMatrix4 const&)` transforms its point, where it discarded the result

0.6.0 (2026-09-06)

**Breaking.** The first release since 0.1.0 in 2000; CMake replaces Autotools, and the Windows build is gone.

**Breaking changes**

* Autotools and the Windows tree are gone; CMake 3.21 presets build gman with C++17
* `GMANDLL` is `GMAN_EXPORT`

**New**

* The seven RenderMan quadrics and convex `Polygon` render, shaded by `matte`, `plastic` or `metal`, lit by `ambientlight`, `distantlight` and `pointlight`, and written as TIFF, PNG, JPEG or PNM
* The RIB front end covers a working subset of RISpec 3.2: camera and transform setup, lights, surface shaders, array and inline-declared parameters, `facevarying` parameters, gzipped streams and `ReadArchive`. An unsupported request warns once and is skipped
* The CMake presets `dev`, `release` and `debug`, with ASan and UBSan, build it
* The licence is LGPL-2.1-or-later, and the name is "GMAN, a RenderMan-compatible renderer"

**Fixed**

* Array parameters reach the renderer, so `fov [45]` takes effect
* Transforms compose in the right order, so an in-world `Rotate` turns about the world origin
* Perspective clipping honours the screen window, so a wide frame draws edge to edge
* Loading two shaders in one scene works

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
