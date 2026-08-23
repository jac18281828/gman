# AGENTS.md

GMAN — a RenderMan-compatible renderer. Read this before changing anything.
This file supersedes `doc/codingguide.txt` (Jan 2001) in force; that file
stays untouched as a historical record.

## Build

Requires CMake 3.25 or newer, a C++23 compiler (clang 16+ or gcc 13+),
libtiff, libpng and zlib. libjpeg is optional. POSIX only: macOS and Linux.

```sh
cmake --preset dev && cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Three presets: `dev` (`build/`, `-Wall -Wextra -Werror` — what the gates
run), `debug` (`build-debug/`, ASan + UBSan), `release` (`build-release/`,
optimized, no sanitizers). Adding, renaming or retargeting a preset is a CI
change — ask first.

## Workflow and approval tiers

**Ask each time, before doing it:** adding or upgrading a dependency, a
cross-module or public-API refactor, deleting a file, changing CI or a
build preset.

**Always ask, no matter how routine it looks:** merging to `main`, opening
a PR, tagging, any force operation. Landing is the operator's, on explicit
approval — never the agent's.

## C/C++ style and design

Correctness first, then idiomatic, reviewable C++. Small functions, early
returns, shallow nesting. Small diffs; no cosmetic churn riding along with
a behavior change. A port that changes results cannot be reviewed, because
a port bug becomes indistinguishable from a logic change — a behavior
change gets its own commit, named as such in the message.

## Comments

No expository or "my way" comments. No comments about the change instead
of the code — "low overhead version," "fully optimal version" and similar
have no place once a diff lands; the commit message carries that history,
not the source. Comments document the code, not the edit that produced it.

## Naming

Semantic, not pattern-based. Avoid suffixes like `State`, `Context`,
`Manager` in new code unless there is a real contrast to draw (`Config` vs
`Runtime`, `Snapshot` vs `Live`). This binds new code only:
`GMANLightSourceMgr`, `GMANGraphicState` and `GMANObjectManager` predate the
rule and are not renamed to satisfy it — renaming a public class is a
cross-module refactor (ask first) with nothing to gain here. Do not use a
prefix or suffix as a namespace: everything in this codebase already starts
with `GMAN` (see "House conventions" below), so that prefix does the one
job a namespace would; do not layer a second one on top of it.

## Abstraction and error handling

Abstract only to remove duplication or encode an invariant. Prefer concrete
domain types over generic wrappers.

Any class that owns a resource through a raw pointer declares — or
explicitly `= delete`s — its copy constructor, copy assignment, move
constructor and move assignment (the Rule of Five). Prefer RAII —
`std::unique_ptr`, `std::string`, standard containers — over raw
`new`/`delete`. This repo has shipped a double-free from a class with owned
raw pointers and no copy constructor, and a leak spanning roughly sixty call
sites from an allocation whose owner threw before reaching its `delete`. A
class that cannot be safely copied, and a function that returns before it
frees, are exactly the shapes those two bugs took.

`GMANParameterList::getPointer` returns NULL for an absent token — an
optional, not an `unwrap`. Code with an optional parameter must check the
returned pointer before dereferencing it (`gmanshaderparams.h`'s
`getFloatParam`/`getColorParam` do this); do not assume presence. It threw
once, and that made every `Projection` without an explicit `"fov"` fail:
`RiWorldBegin`'s own default-to-90 path was unreachable, because the lookup
feeding it never returned. Absence is the routine case here, and a
throw is the wrong shape for it.

## Dependencies and includes

Prefer the standard library. System headers `< >` before GMAN headers
`" "`. Qualify `std::` explicitly — no blanket `using namespace std`, and
no per-name `using std::string` either. The blanket form in a public header
is what broke this codebase's namespace correctness once already.

## Tests

Test behavior and contracts, not language or library internals. Avoid
vacuous tests: removing or breaking the target code must cause a test to
fail — this project has shipped three defects past green assertions, each
one caught only by human or adversarial review, never by CI. Unit tests
hermetic: no network, no files outside the checked-in tree. Add or update
tests for every behavior change.

## RIB authoring

RIB fixtures and the renderer's own coordinate math are this project's
genuine difficulty.

**Handedness and matrix convention.** RenderMan's camera looks down `+z`,
left-handed: a point in front of the camera has positive camera-space z.
The CTM chain is row-vector, `p * M`, with translation in row 3
(`GMANMatrix4::trans`/`rot`/`scale`, matching `GMANMatrix4::p3m`/`p4m` and
`GMANMatrix4::concat`). The projection stage (`prjPersp`/`prjOrtho`,
consumed by `GMANVector4::projTransform`) uses a different, equally
pre-existing layout — do not mix them: a CTM fed through `projTransform`'s
formula, or a projection matrix fed through `p3m`'s, silently produces a
plausible-looking wrong image (see `libgman/gmanmatrix4.cpp`'s comments at
`prjPersp`/`p3m` for the two formulas side by side). `RiWorldBegin` does not
reset the CTM to identity, so a primitive's own CTM already carries
world-to-camera in one product, not two — do not re-apply the world-to-
camera transform on top of it.

Object-space normals (what `getNormal(u,v)` returns on a primitive)
transform by the inverse transpose of the CTM — `createParametric` builds
`ctmInv` for exactly this. Face normals (`GMANFace::calcNormal()`) do not:
`calcNormal()` runs *after* the face's vertices are already in camera
space, so the cross product of two already-transformed edges needs no
separate transform at all. Treat these as two different rules for two
different kinds of normal, not one rule with an exception.

**The camera convention for fixtures.** Every hand-written scene in
`tests/rib/` puts the camera at a working distance before `WorldBegin` —
`Translate 0 0 5`, as in `sphere.rib`, `lights.rib` and
`partial_sphere.rib`. A RIB with no transform before `WorldBegin` puts the
camera inside the geometry.

**One scene, one thing.** Each fixture tests a single behavior.

**The desync convention.** A request fixture is "the request under test,
then a `Sphere`": a handler that mis-counts its own parameters desyncs the
token stream, and that failure surfaces as the *following* request failing
to parse — a request tested in isolation misses it entirely. This is the
one home for this rule; `tests/rib/README` and `tests/ribdialect_test.cpp`
both point here instead of restating it.

**Third-party RIB.** Record source, upstream commit and license in
`tests/rib/README`. Fetch once and check in; tests are hermetic and do not
reach the network.

**Explicit `Clipping` where geometry is flat or narrow in z.** Not because
the spec requires it, but because this renderer has a known near-clip
precision defect (`SPEC.md` §8) that corrupts such geometry at the default.
Pair affected geometry with `Clipping <near> <far>` using a `near` that is
not astronomically small (`0.5`, not the default) — e.g. `Clipping 0.5 50`.

**What the RIB front end actually supports.** Do not hand-maintain a
request-support table here — the last one drifted false in over a dozen
places and stayed that way until this rewrite. `libgman/gmanribtokenize.cpp`
(`parseKeyword`) is ground truth for which keywords the tokenizer
recognizes at all; `libgman/gmanribparse.cpp`'s request switch is ground
truth for which of those actually reach a renderer call versus parse and
get ignored. `SPEC.md` records the currently-known gaps between the two —
including tokens with a parser handler already wired but historically
missing from the tokenizer, and requests with neither.

**Shader plugin authoring.** Subclass `GMANSurfaceShader`
(`gmansurfaceshader.h`) and implement `computeCi`/`computeOi`; read your own
parameters off the protected `GMANParameterList pl` your `GMANShader` base
carries, through `gmanshaderparams.h`'s helpers rather than
`GMANParameterList::getPointer` directly (see "Abstraction and error
handling" above). Export the two `extern "C"` symbols `GMANGetLoadableInfo`
and `GMANLoadShader` (returning a `GMANShader*`); build the plugin as a
`MODULE` library via `gman_add_plugin` in `CMakeLists.txt`, named
`lib<name>.so` — `RiSurface "<name>"` dlopens exactly that name.
`shaders/gmanmatte.cpp`, `gmanplastic.cpp` and `gmanmetal.cpp` are the
reference implementations. Everything a shader sees is camera space (see
"Handedness and matrix convention" above) — a shader that mixes spaces
produces a plausible-looking wrong picture, not a crash.

## Gates

Not complete until every one is green.

```sh
cmake --preset dev && cmake --build build --parallel
cmake --preset debug && cmake --build build-debug
ctest --test-dir build --output-on-failure
CXX=g++ cmake --preset dev -B build-gcc && cmake --build build-gcc
valgrind --error-exitcode=1 --track-origins=yes --leak-check=summary \
  ./build/gman tests/rib/sphere.rib
valgrind --error-exitcode=1 --track-origins=yes --leak-check=summary \
  ./build/gman tests/rib/corpus/menger.rib
```

Three CI workflows gate every push: `ci` (jobs `build`, `sanitizers`,
`valgrind`), `commitlint`, and `Yamlfmt`. `tests/docsconsistency_test.cpp`
keeps this block and `.github/workflows/ci.yml` from drifting apart.

If a gate is still red after a genuine fix attempt, stop and report the
actual error rather than iterating on guesses.

## Commits and landing

Conventional Commits, signed. `type(scope): subject`, type and scope and
subject all lower-case, no trailing period. Subject and every body line
wrapped at 80 columns.

All commits land on a branch; `main` only ever sees a fast-forward. Merging
is the operator's, on explicit approval — never the agent's.

## House conventions

From `doc/codingguide.txt` (Jan 2001, John Cairns), which stays as a
historical record. This file supersedes it. Still in force:

- All source filenames lower-case.
- Bicapitalized identifiers: `methodName`, `SymbolName`.
- `GMAN` prefix on every global-scope class and object name.
- Implementation in `.cpp`, except templates and code deliberately inlined.
- RenderMan API types throughout; a new `GMAN` type for anything the RI type
  system does not cover.
- Header files guarded against multiple inclusion.

**Rule 2 is retired.** It required every class to inherit
`UniversalSuperClass` "to support the (future) addition of memory
management". The memory management never arrived. What the class actually
carried was logging, which now lives in `gmanlog.h` as free functions. Do
not reintroduce a universal base class.
