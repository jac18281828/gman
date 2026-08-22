# AGENTS.md

GMAN — a RenderMan-compatible renderer. Read this before changing anything.

## Build

Requires CMake 3.25 or newer, a C++23 compiler (clang 16+ or gcc 13+),
libtiff, libpng and zlib. libjpeg is optional. POSIX only: macOS and Linux.
Windows was deleted, not deprecated.

```sh
cmake --preset dev && cmake --build build --parallel
```

Three presets, with pinned binary directories the gate list depends on:

| Preset | Binary dir | What it is |
|---|---|---|
| `dev` | `build` | `-Wall -Wextra -Werror`. What the gates run. |
| `debug` | `build-debug` | ASan + UBSan |
| `release` | `build-release` | optimized, no sanitizers |

Options: `GMAN_WITH_TIFF`, `GMAN_WITH_PNG`, `GMAN_WITH_JPEG` select image
drivers. `GMAN_BUILD_REYES`, `GMAN_BUILD_RAYTRACER` and `GMAN_BUILD_RADIOSITY`
are `OFF` by default. All three renderers are non-functional and are kept for
the design intent a later phase may want; reyes and raytracer additionally do
not link, because each declares virtuals that were never defined.

## Gates

Not complete until every one is green.

```sh
cmake --preset dev && cmake --build build --parallel        # zero warnings, -Werror
cmake --preset debug && cmake --build build-debug           # ASan + UBSan
ctest --test-dir build --output-on-failure
CXX=g++ cmake --preset dev -B build-gcc && cmake --build build-gcc
valgrind --error-exitcode=1 --track-origins=yes --leak-check=summary \
  ./build/gman tests/rib/sphere.rib                         # uninitialized reads
valgrind --error-exitcode=1 --track-origins=yes --leak-check=summary \
  ./build/gman tests/rib/corpus/menger.rib                  # ditto, parser path
./build/gman tests/rib/sphere.rib                           # the runtime baseline
```

The last line is an observation, not a pass/fail. Its assertions live in the
`baseline` ctest case, which is where the real bar sits. Do not add a shell
check like `test -s sphere.tif`: it passes on a truncated file and undercuts
the actual test.

If a gate is still red after a genuine fix attempt, stop and report the actual
error rather than iterating on guesses.

## Runtime baseline

Observed in phase 0, on the first modern build of this tree:

```
$ ./build/gman tests/rib/sphere.rib
exit status 1
no file written
ERROR: RIE_CONSISTENCY -- GMANParameterList: TOKEN_NOT_FOUND
```

`GMANRIBParse::parseParameterList` discarded bracketed array parameters — the
`LEFT_BRACKET` branch parsed the array and dropped it under a FIXME. So
`"fov" [45]` never reached the projection's parameter list, and
`RiWorldBegin`'s lookup of `RI_FOV` threw out of
`GMANParameterList::getPointer`, which threw where its caller expected NULL.
No renderer ran.

**Fixed in phase 2.** `parseParameterList` now builds real arrays for both the
bracketed and unbracketed forms:

```
$ ./build/gman tests/rib/sphere.rib
exit status 0
sphere.tif written
```

Phase 2 also found and fixed a second, unrelated bug this path exposed for
the first time: `GMANBody::~GMANBody()` (`libgman/gmanbody.cpp`) reassigned
its walk pointer to the surface it had just freed rather than to that
surface's `next`, so any body with a surface use-after-freed on teardown.
Nothing before phase 2 ever got far enough to construct and then destroy a
real primitive to hit it — the fix landed here, one file outside phase 2's
named scope, because without it the phase's own "exits 0" goal is
unreachable for any RIB containing geometry.

The image `sphere.tif` holds is not yet correctly projected, lit or shaded —
the four severed links SPEC.md documents (transform application, face
normals, backface culling, the perspective matrix) are all still broken.
`tests/baseline.cpp` pins today's exit-0-and-a-real-file behavior and names
phase 1 and phase 3 as the phases that make the image itself correct.

## RIB support

The honest answer to "what does GMAN support." Three buckets: requests that
reach a renderer call that does real work, requests that are recognized and
consumed but never render (a stub `RiXxxV` body, or phase 2's deliberate
parse-and-ignore policy), and requests GMAN has never heard of.

**Renders** (reaches `GMANObjectManager`/`GMANWorldManager` or otherwise
changes state the renderer reads): `Sphere`, `Cone`, `Cylinder`,
`Hyperboloid`, `Paraboloid`, `Torus`, `Polygon`, `Patch`; `Translate`,
`Rotate`, `Scale`, `Transform`, `ConcatTransform`, `Identity`, `Color`,
`Opacity`, `Sides`, `Orientation`, `ReverseOrientation`; `Display`, `Format`,
`Projection`, `FrameAspectRatio`, `ScreenWindow`, `CropWindow`, `Clipping`,
`DepthOfField`, `Shutter`, `PixelSamples`, `Exposure`, `Quantize`,
`Basis`; `WorldBegin`/`End`, `FrameBegin`/`End`, `AttributeBegin`/`End`,
`TransformBegin`/`End`, `SolidBegin`/`End`, `ObjectBegin`/`End`,
`ObjectInstance`, `MotionBegin`/`End`, `Illuminate`, `Declare`.
*Rendering here means the geometry or state reaches storage, not that the
image comes out correctly* — see SPEC.md §2's four severed links (transform
application, face normals, backface culling, the perspective matrix), all
still open, all Phase 1/3.

**Parses, never renders:**

- *Stub `RiXxxV` bodies, pre-dating phase 2:* `GeneralPolygon`,
  `PointsPolygons`, `PointsGeneralPolygons`, `PatchMesh`, `NuPatch`, `Disk`,
  `Points`, `LightSource`, `AreaLightSource`, `Attribute`, `Deformation`,
  `CoordSysTransform`, `TransformPoints`, `Option`, `Hider`, `Surface`,
  `Atmosphere`, `Interior`, `Exterior`, `Displacement`, `Imager`,
  `ShadingRate`, `ShadingInterpolation`, `GeometricApproximation`. All parse
  correctly; none is wired to draw or shade (Phase 3, "All shading" per
  SPEC.md §2).
- *Phase 2's own parse-and-ignore additions* (settled decision, not a stub —
  drawing these is explicitly Scope C / SPEC.md §4, not this phase):
  `Curves`, `Blobby`, `SubdivisionMesh`, `Procedural`, `SolidBegin`/`End`
  (both consumed; `RiSolidBegin`/`End` above already state-track the block,
  the *shape* a solid represents is not evaluated), `Detail`, `DetailRange`,
  `RelativeDetail`, `Skew`, `Matte`, `TrimCurve`, `ErrorHandler`,
  `ArchiveRecord`, `MakeTexture`, `MakeBump`, `MakeLatLongEnvironment`,
  `MakeCubeFaceEnvironment`, `MakeShadow`, `IfBegin`, `ElseIf`, `Else`,
  `IfEnd`.
- `ReadArchive` recurses the parser over the archive (relative to the
  including file, then as given) with cycle detection and a depth cap; its
  own content renders or not exactly like the top-level file's does.

**Unrecognized:** anything else — RIS-era requests (`Bxdf`, `Integrator`,
`Resource`, `ShaderLayer`; SPEC.md §4), vendor extensions, and any request a
future RISpec revision adds. Warned once by name (not once per occurrence)
and skipped; the full set skipped in a given file is reported once at the
end of the parse. Never aborts the parse. See
`libgman/gmanribtokenize.cpp`'s `parseKeyword` and
`GMANRIBParse::skipUnknownRequest`.

**Binary RIB and `.rib.gz`:** `.rib.gz` (or any RIB gzip'd regardless of its
name — detected by magic bytes) decompresses transparently. Binary-encoded
RIB (RISpec's own compact token encoding, distinct from gzip) is not
implemented; GMAN never needed it for the corpus this phase tested against.
GMAN never writes binary RIB, and never will (`libgmanrib/gmanascii.cpp`
writes ASCII only).

## House conventions

From `doc/codingguide.txt` (Jan 2001, John Cairns), which stays as a
historical record. This file supersedes it. Still in force:

- All source filenames lower-case.
- Bicapitalized identifiers: `methodName`, `SymbolName`.
- `GMAN` prefix on every global-scope class and object name.
- Implementation in `.cpp`, except templates and code deliberately inlined.
- System headers with `< >` before GMAN headers with `" "`.
- RenderMan API types throughout; a new `GMAN` type for anything the RI type
  system does not cover.
- Header files guarded against multiple inclusion.

**Rule 2 is retired.** It required every class to inherit
`UniversalSuperClass` "to support the (future) addition of memory management".
The memory management never arrived. What the class actually carried was
logging, which now lives in `gmanlog.h` as free functions. Do not reintroduce
a universal base class.

Qualify `std::` explicitly. No blanket `using namespace std`, and no per-name
`using std::string;` either. The blanket form in a public header is what broke
this codebase's namespace correctness once already.

## Commits

Conventional Commits, signed. `type(scope): subject`, type and scope and
subject all lower-case, no trailing period. Subject and every body line wrapped
at 80 columns.

## Scope discipline

The back end was never built. When touching the front end, do not let a syntax
or build change also alter what the program computes — a port that changes
results cannot be reviewed, because a port bug becomes indistinguishable from a
logic change. Behavior changes get their own commit and get named in the
message.

Defects deliberately preserved, each marked `TODO(phase-1)` at the site:

- `libgman/gmanrendermanimpl.cpp` — an inner `RtFloat fov` shadows the outer
  one in the `RiProjection` handler, so FOV is always 90.
- `libgman/gmangraphicstate.cpp` — `cmdTransformPoints`'s mask is written
  `B|F|W|A|T||S`; the `||` collapses the whole expression to 1.
- `libgman/gmangraphicstate.cpp` — `if (motionError=true)` assigns where it
  means to compare, so control always returns.
