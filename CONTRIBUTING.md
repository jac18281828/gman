# Contributing to GMAN

Contributions are welcome — a bug report, a fix, a scene that renders wrong,
or a whole subsystem. GMAN was written in 1999, shelved in 2002 and revived in
2026, so there is plenty left to do and no shortage of places to start.

## Where the standard lives

**[`AGENTS.md`](AGENTS.md) is the coding standard.** Build commands, the gate
list, C++ style, naming, error handling, the test layout and the RIB authoring
rules all live there, and it is the file to read before changing anything. This
document covers how to get a change in; that one covers what the change has to
look like.

## We develop on GitHub

GitHub hosts the code, the issues and the pull requests.

1. Fork the repo and create your branch from `main`.
2. Add or update tests for every behavior change.
3. Update the docs when you change what they describe.
4. Make every gate pass — see below.
5. Open the pull request.

`main` only ever fast-forwards. Rebase rather than merge.

## Make the gates pass

Not complete until every one is green. `AGENTS.md`'s Gates section is the
authority; the short version:

```sh
cmake --preset dev && cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CI runs more than that — a sanitizer build, a valgrind leg, gcc as well as
clang, `clang-format`, and commitlint. The devcontainer in `.devcontainer/`
carries the same toolchain, so `./build.sh` reproduces the whole set locally
before you push. On macOS this is worth preferring: Apple clang does not
diagnose what gcc and libstdc++ do.

## Tests have to have teeth

A test that still passes when you break the code it covers is worse than no
test, and this project has shipped defects past green assertions before. Break
the thing on purpose, watch the test go red, put it back. Unit tests are
hermetic: no network, no files outside the checked-in tree.

Golden images are checked in, never generated on demand. Regenerating one is
legitimate only when the pixels moved because of an intended change you can
explain — never to turn a red test green.

## Commits

Conventional Commits, signed, subject and body wrapped at 80 columns:

```
fix(gman): compose local transforms ahead of the accumulated ctm
```

Type, scope and subject all lower-case, no trailing period. commitlint gates
this. A behavior change gets its own commit, named as such — a fix that also
reformats cannot be reviewed, because the bug becomes indistinguishable from
the noise.

## Reporting bugs

Open an issue. A good report has a summary, the steps to reproduce, what you
expected, and what actually happened.

For a rendering bug, **the `.rib` file is the reproduction** — a scene small
enough to read beats a description of one. Say which primitives are involved,
attach the image you got, and describe the image you expected. `tests/rib/`
shows the house style for a minimal scene.

## Coordinate math is the hard part

Most of this renderer's real difficulty is in the coordinate chain and RIB
authoring, and both have rules that are not guessable from the code. Read
`AGENTS.md`'s "RIB authoring" section before touching a transform, a normal, a
projection or a clip plane. A scene that renders a plausible-looking wrong
picture is the characteristic failure here, and it is much easier to avoid than
to debug.

## License

GMAN is distributed under the GNU Lesser General Public License, version 2.1
or later. By contributing you agree that your contributions are licensed under
the same terms. Every source file carries an SPDX tag and the licence notice;
new files should carry both, and your own copyright line.

---

Adapted from the open-source contribution guidelines for
[Facebook's Draft](https://github.com/facebook/draft-js).
