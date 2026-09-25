# AGENTS.md

GMAN — a RenderMan-compatible renderer in C++20. POSIX only: macOS and Linux.

## Workflow
1. Summarize current behavior and invariants before proposing edits.
2. **Ask each time** — dependencies, cross-module or public-API refactors, file
   deletions, CI or build-preset changes.
3. **Always ask** — merging to `main`, opening a PR, tags, force ops.

## C++ Style & Design
- Correctness first; then idiomatic, reviewable C++.
- Small functions, early returns, shallow nesting.
- Small diffs, no cosmetic churn. A behavior change gets its own commit, named
  as such.
- `const auto x = expr;` when the type is evident; spell the type for a
  narrowing conversion, an `initializer_list` or a literal whose type matters.
- New code writes `T const&` and `T const*`.
- Build strings with `std::string` and `std::format`: no `sprintf`/`strcpy`/
  `strcat` family, no `new char[`, no `PATH_MAX` outside `tests/`.
- Prefer RAII. A class owning a raw pointer declares or deletes all five
  special members.
- Comments document the code, not the change. No expository or 'my way'
  comments.
- clang-format 22.1.8 keeps the tree formatted to `.clang-format`; a change
  keeps it so. `format-check` names `pip install clang-format==22.1.8` for a
  local run and fails on any other version.

## Naming
- Semantic, not pattern-based. Avoid `State`, `Context`, `Manager` without a
  real contrast. Existing names stay.
- The `GMAN` prefix marks global scope: every global-scope class carries it,
  and code in a namespace sheds it, as `gman::gammaCorrected` and
  `gmanshader::plastic` do. A `libgman/` header declares its names in
  `namespace gman`. `ri.h` stays global; the RenderMan interface fixes it
  there.
- Lower-case filenames; `methodName`, `SymbolName`. A shader class takes its
  RIB token as its name: `Surface "plastic"` loads `gmanshader::plastic`.

## Abstraction
- Abstract only to remove duplication or encode an invariant.
- Prefer concrete domain types over generic wrappers.
- `GMANParameterList::getPointer` returns NULL for an absent token; check it,
  or use `gmanshaderparams.h`'s helpers.
- No universal base class; logging is the free functions in `gmanlog.h`.

## Seams
- Threads: only `gman::parallelFor` (`libgman/gmanparallel.h`).
  `libgman/gmanparallel.cpp` and `libgman/gmanlog.cpp` alone may name a thread
  primitive; `tests/threadcontainment_test.cpp` enforces it.
- libtiff: only `libgman/gmantiff.cpp` includes `<tiffio.h>` (`tests/` exempt).
- Symbol visibility follows `BUILD_TESTING`. A test build exports every
  symbol, so tests reach internals; a build with tests off exports only what
  `GMAN_EXPORT` tags, and `release.yml` ships that build. A declaration
  production code uses outside its own library carries `GMAN_EXPORT`; a
  test's use does not.

## Dependencies and Includes
- Prefer the standard library. Qualify `std::`; no `using` of `std` names.
- Four include blocks, sorted: standard library, system, libraries, gman.
  `.clang-format` encodes it.

## Tests
- Test behavior and contracts, not language or library internals.
- Avoid vacuous tests: breaking the target code must fail a test.
- Hermetic: no network, no files outside the tree.
- Add or update tests for every behavior change.
- One `tests/<name>_test.cpp` per test, using `tests/check.h`; golden renders
  use `tests/goldenimage.h`. Register it in `tests/CMakeLists.txt` with
  `LABELS` (`render` if it runs `gman`, else `unit`) and a `TIMEOUT` of at
  least 30.
- Pin a known defect with `WILL_FAIL TRUE` asserting the wanted behavior;
  remove it with the fix.
- Regenerate a golden image only for a reviewed behavior change, and say so
  in the commit.

## RIB authoring
- **Handedness and matrix convention.** Camera looks down `+z`, left-handed.
  CTM is row-vector `p * M`, composed `CTM_new = Local . CTM_old`; the
  projection matrices (`prjPersp`/`prjOrtho`) use a different layout, so never
  mix them. `RiWorldBegin` does not reset the CTM. Shaders see camera space.
- Primitive normals transform by the CTM's inverse transpose; face normals are
  computed already in camera space.
- Fixtures put the camera at `Translate 0 0 5` before `WorldBegin`.
- One scene, one thing.
- **The desync convention.** A request fixture is the request under test, then
  a `Sphere`, so a mis-counted parameter list fails the next parse.
- Flat or z-narrow geometry needs `Clipping 0.5 50`: a known near-clip
  precision defect.
- Third-party RIB: record source, commit and license in `tests/rib/README`.
- Supported requests: `gmanribtokenize.cpp` and `gmanribparse.cpp` are ground
  truth.
- Shader plugins: subclass `GMANSurfaceShader` in `namespace gmanshader`;
  define `GMANGetLoadableInfo`, `GMANLoadShader` and `GMANDestroyShader` at
  global scope as `extern "C" GMAN_EXPORT`, build with `gman_add_plugin`.
  See `shaders/gmanmatte.cpp`.

## Completion Gates

Before marking work complete, run and report:

```sh
cmake --preset dev && cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --build build --target format-check
```

CI covers the rest: `build` (clang and gcc), `sanitizers`, `valgrind`,
`format-check`, `drivers-off`, plus `commitlint` and `Yamlfmt`.

If a gate stays red after a genuine fix, stop and report the error.

## Commits
- Conventional Commits, signed, lower-case `type(scope): subject`, wrapped at
  100 columns: commitlint's limit, not code's 120.
- All commits land on a branch; `main` only fast-forwards.
