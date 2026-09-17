# Contributing to GMAN

Contributions are welcome: a bug report, a fix, a scene that renders wrong or
a whole subsystem. Plenty is open — shadows, trimmed `NuPatch`, `Points` and
`Curves`, a renderer that threads through `gmanParallelFor`, and most of the
RISpec beyond the primitives CHANGELOG.md records as rendering.

## Pull requests

1. Fork the repo and branch from `main`.
2. Add tests for anything that changes behavior.
3. Update the docs when you change what they describe.
4. Make the checks below pass.
5. Open the pull request.

`main` only ever fast-forwards. Rebase rather than merge.

## Before you push

```sh
cmake --preset dev && cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --build build --target format-check
```

Green on all three, and green in CI. CI also runs a sanitizer build, valgrind
and gcc alongside clang, so it catches what a local macOS build cannot. The
devcontainer in `.devcontainer/` carries the same toolchain if you want the
whole set locally: `./build.sh`.

## Tests

Add tests for behavior changes, and prove each one fails when its target
breaks: break the code on purpose, watch the test go red, put it back. A
vacuous test covers nothing. Tests are hermetic: no network, no files outside
the checked-in tree.

Golden images are checked in. Regenerate one only when the pixels moved for a
reason you can explain, never to turn a red test green.

## Commits

[Conventional Commits](https://www.conventionalcommits.org), signed.
`commitlint.config.js` holds the rules and CI runs them, so let it tell you
rather than reading a list here.

## Style

The tree is clang-format clean, pinned to 22.1.8
(`pip install clang-format==22.1.8`); format before you commit. Otherwise
match the file you are in: lower-case filenames, `GMAN`-prefixed globals,
`methodName` and `SymbolName`, implementation in `.cpp`, RenderMan API
types, guarded headers.

## Bug reports

Open an issue with a summary, the steps to reproduce, what you expected and
what you got.

For a rendering bug, **the `.rib` file is the reproduction**. A scene small
enough to read beats a description of one. Attach the image you got and say
what you expected instead. `tests/rib/` shows the house style for a minimal
scene.

## Working with an AI agent

`AGENTS.md` is the brief for AI agents working in this repo: the conventions
at length, and the coordinate-space traps that are expensive to rediscover.
Point your agent at it.

## License

GMAN is distributed under the GNU Lesser General Public License, version 2.1
or later. By contributing you agree that your contributions are licensed under
the same terms. Every source file carries an SPDX tag and the licence notice;
new files should carry both, plus your own copyright line.

---

Adapted from the open-source contribution guidelines for
[Facebook's Draft](https://github.com/facebook/draft-js).
