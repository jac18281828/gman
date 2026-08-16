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

`GMANRIBParse::parseParameterList` discards bracketed array parameters — the
`LEFT_BRACKET` branch parses the array and drops it under a FIXME. So
`"fov" [45]` never reaches the projection's parameter list, and
`RiWorldBegin`'s lookup of `RI_FOV` throws out of
`GMANParameterList::getPointer`, which throws where its caller expects NULL.
No renderer runs.

An earlier prediction that GMAN would write an entirely black 640×480 TIFF was
traced from source and never run. It is wrong. `tests/baseline.cpp` pins what
actually happens, and names phase 1 as the phase that supersedes it.

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
