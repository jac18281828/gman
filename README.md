# GMAN — Realistic RenderMan Renderer

[![ci](https://github.com/jac18281828/gman/actions/workflows/ci.yml/badge.svg)](https://github.com/jac18281828/gman/actions/workflows/ci.yml)

Begun in [1999](https://gman-toolkit.sourceforge.net), revived in [2026](https://2ad.com/gman.html).

[![gman's path-traced render of samples/vase.rib](samples/vase.png)](https://2ad.com/gman.html)

## Install

GMAN 1.3.0 ships as a tarball for Linux x86_64, Linux arm64 and macOS arm64 on the
[releases page](https://github.com/jac18281828/gman/releases). gman links libtiff, libpng, libjpeg
and zlib at run time. On Linux:

```sh
sudo apt-get install libtiff-dev libpng-dev libjpeg-dev zlib1g-dev
```

On macOS:

```sh
brew install libtiff libpng jpeg
```

Download the tarball for your platform, unpack it, `cd` into the folder and run the installer:

```sh
tar xzf gman-1.3.0-linux-x86_64.tar.gz
cd gman-1.3.0-linux-x86_64
sudo ./install.sh
```

`install.sh` installs `bin/`, `lib/` and `include/`, that is the `gman` program, its renderer and
shader plugins, the three libraries below and their headers, into `/usr/local`, which needs `sudo`.
It then runs the installed `gman --version`, and on macOS clears the download quarantine from the
files it wrote.

- `./install.sh --prefix "$HOME/.local"` installs into a directory of your own, with no `sudo`.
- `sudo ./install.sh --uninstall` removes exactly the files the install wrote, and each directory
  it created that is now empty. `--prefix` names the same directory in both commands. The list of
  files is `share/gman/install_manifest.txt` under the prefix, and installing over an earlier
  install removes that one first.
- `install.sh` leaves your `PATH` and shell profile alone. When the prefix's `bin` is not on
  `PATH`, it says so and you add it.
- `samples/` stays in the unpacked folder: `install.sh` does not install it, and the next section
  renders from it.

`lib/` holds three shared libraries; `libgman_ray` and `libgman_raster` build on `libgman`:

- `libgman`, the RenderMan interface, the RIB parser, the shader and renderer contracts and the
  image writers. Every program that links gman needs it.
- `libgman_ray`, the ray geometry, the BVH, the emitters and the indirect-light pass seam that the
  ray tracer, the path tracer and the radiosity pass share.
- `libgman_raster`, the body, face and vertex model, the patch and polygon object manager and the
  polygon clipper that the z-buffer renderer uses.

The renderers and shaders sit beside them as `lib<name>.so` plugins, which `gman` loads by bare
name. On macOS a `lib<name>.so` in the directory you run `gman` from wins over the installed one, so
run `gman` from a folder that holds no older gman `lib*.so`.

To build gman yourself and install it with CMake, see [Build from source](#build-from-source).

## Try it

From the unpacked folder:

```sh
gman -r gmanraytracer samples/vase.rib
```

That writes `vase.png` to the current directory, a quick ray-traced preview of the picture at the
top, in a few seconds. The floor tiles come from `samples/floor16.tif`, a 16-bit TIFF that gman
reads at full depth.

The scene sets its own render: `Format 1280 800`, `PixelSamples 2 2`,
`PixelFilter "catmull-rom" 3 3` and `Quantize "rgba" 255 0 255 0`. Catmull-Rom keeps edges sharper
than gman's default Gaussian filter, and the zero dither amplitude keeps the PNG small.

## Path trace it

Add one line to `samples/vase.rib` in the unpacked folder, under `PixelSamples 2 2`, and render it
through the path tracer:

```
Option "pathtracer" "integer samples" [64]
```

```sh
gman -r gmanpathtracer samples/vase.rib
```

That overwrites `vase.png` with the picture at the top. Sixty-four paths through each of the four
subpixels follow the light as it bounces off the walls, floor and table. The path tracer renders
rows on every core, about 40 seconds on an Apple M3 Max, where the ray tracer takes two. It
computes the bounce light the scene's `ambientlight` stood in for, so it skips that light and says
so:

```
gmanpathtracer: ambientlight lights nothing under the path tracer; skipped 1 light(s).
```

## Light it with area lights

`samples/materials.rib` sets four materials before a clay wall and lights them with two area
lights, a warm disk overhead and a small blue sphere lamp at the left:

```sh
gman -r gmanpathtracer samples/materials.rib
```

From the left: gold `shinymetal`, `glass`, a `mirror` that takes the wall's clay colour, and a
`plastic` torus. The overhead disk pools a soft shadow under each one. The scene traces 64 paths a
subpixel at 640 by 400, set by `Format` and `Option "pathtracer" "integer samples"` at the top of
the file.

The same materials make a still life in 
[still-life-material](https://github.com/jac18281828/still-life-material), with a marble floor, a
generated cactus and a glass ball that lenses it:

[![gman's path-traced still life: a gold-leaf ball, a glass ball, a potted cactus, a mirror ball and a blue torus on a marble floor](samples/still-life.png)](https://github.com/jac18281828/still-life-material)

The picture is `make hq`'s render in that repository, 2,048 paths a pixel at 1920 by 1200, scaled to
1280 by 800. A shader built there turns the gold ball to gold leaf.

## Poke it

Change one line of `samples/vase.rib` in the unpacked folder and render it again with
`gman -r gmanraytracer samples/vase.rib`. Each picture starts from the original scene.

| | |
|---|---|
| <img src="samples/poke/glass.png" width="320" alt="the vase in glass"><br>**Glass vase.** In the `## Vase` block, make the surface `Surface "glass"` and delete the `Opacity`. | <img src="samples/poke/mirror.png" width="320" alt="the robot's dome as a mirror"><br>**Mirror dome.** Under `# head dome`, make the surface `Surface "mirror" "Kr" [1]`. |
| <img src="samples/poke/sunlight.png" width="320" alt="the room lit by sunlight"><br>**Sunlight.** Swap the lamp for the sun: `LightSource "distantlight" 2 "intensity" [1.2] "lightcolor" [1 0.95 0.83] "from" [1 3 10] "to" [0 0 1]`. The walls now shadow the room. | <img src="samples/poke/widefov.png" width="320" alt="the room through a wider lens"><br>**Wider lens.** `Projection "perspective" "fov" [55]`. |
| <img src="samples/poke/onesample.png" width="320" alt="the scene at one sample a pixel"><br>**One sample a pixel.** `PixelSamples 1 1`, and the edges go jagged. | <img src="samples/poke/zbuffer.png" width="320" alt="the scene through the z-buffer renderer"><br>**The z-buffer.** No edit: plain `gman samples/vase.rib` renders the fast preview, without shadows, reflection or refraction. |

### A first scene

Eleven lines, from nothing:

```
Display "first.png" "file" "rgba"
Format 640 400 1
Projection "perspective" "fov" [30]
Translate 0 0 6
WorldBegin
LightSource "ambientlight" 1 "intensity" [0.2]
LightSource "distantlight" 2 "from" [-2 2 -3] "to" [0 0 0]
Surface "plastic"
Color [0.9 0.25 0.2]
Sphere 1 -1 1 360
WorldEnd
```

```sh
gman -r gmanraytracer first.rib
```

<img src="samples/poke/first.png" width="320" alt="a red plastic sphere">

## What GMAN renders

- **Renderers.** The default `gmanzbuffer`, `gmanraytracer` and `gmanpathtracer`.
- **Geometry.** Polygons, the seven quadrics and, under the z-buffer, bilinear, bicubic and NURBS
  patches. Per-vertex `"N"` shades smoothly.
- **Surfaces.** `matte`, `plastic`, `metal`, `shinymetal`, `glass`, `mirror` and `paintedplastic`,
  which takes texture, opacity and normal maps.
- **Lights.** Ambient, distant, point, spot and area lights.
- **Imager.** `background`, which fills each pixel's uncovered part.
- **Bounce light.** The path tracer, or a radiosity pass under the ray tracer:
  `Option "render" "string indirect" ["radiosity"]`.
- **Antialiasing.** `PixelSamples` through a box, triangle, Gaussian, Catmull-Rom or sinc filter.
- **Textures.** Maps written by `MakeTexture` and `MakeLatLongEnvironment`.
- **RIB.** Plain or gzip'd, with `ReadArchive`.
- **Output.** TIFF at 8 bits, 16 bits or float, PNG and PNM at 8 or 16 bits, JPEG, and OpenEXR as an
  opt-in beta build option.

## Render

`gman scene.rib` renders a RIB file through the default z-buffer renderer,
`gmanzbuffer`. `-r gmanraytracer` renders the same file through the ray
tracer instead, with real reflection, refraction and shadows, and
`-r gmanpathtracer` through the path tracer, which traces
`Option "pathtracer" "integer samples" [n]` paths per pixel. Output lands
wherever the scene's own `Display` request names, relative to the current
directory. `gman --bit-depth 16 scene.rib` writes 16-bit TIFF, PNG and PNM
when the scene sets no `Quantize`; `--bit-depth 8`, the default, keeps 8. A
scene selects 16 bits itself with `Quantize "rgba" 65535 0 65535 0.5`, and
an explicit `Quantize` that contradicts the flag stops the file with an
error.

`-d`, `-i`, `-w`, `-e` and `-q` set the log level to debug, info (the
default), warning, error or disaster-only; `-l` also writes a log file named
for the RIB; `-h` prints usage; `--version` prints the version and the
drivers built in. Each flag has a long name: `--help`, `--debug`, `--info`,
`--warning`, `--error`, `--quiet`, `--log-file`, `--renderer` and
`--bit-depth`; the last two also take `--renderer=NAME` and
`--bit-depth=BITS`.

At the default `Clipping`, flat or narrow-z-range geometry renders
corrupted or blank; pair it with an explicit `Clipping <near> <far>`.

## Build from source

Requires CMake 3.24 or newer, a C++20 compiler with `<format>` (GCC 13 or
Clang 17 or newer), libtiff, zlib and CLI11 2.4 or newer. CMake uses an
installed CLI11 and otherwise downloads a pinned release at configure time,
checked by its SHA-256. libpng and libjpeg are optional: a build without one
rejects that `Display` extension with `RIE_BADFILE`, and `gman --version`
lists the drivers compiled in. POSIX only: macOS and Linux.

On Linux, install the libraries first:

```sh
sudo apt-get install cmake libtiff-dev libpng-dev libjpeg-dev zlib1g-dev libcli11-dev
```

On macOS, `brew install cli11` installs CLI11.

```sh
cmake --preset dev
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`AGENTS.md`'s Tests section covers the test layout, adding a new test and
golden-image regeneration; its Completion Gates section lists the local
gates, and says CI covers the rest.

`samples/vase.rib`'s `Display` writes PNG, which needs libpng at build
time: without it, `gman samples/vase.rib` rejects the scene with
`RIE_BADFILE`; install libpng and reconfigure with `-DGMAN_WITH_PNG=ON` to
fix it.

To install into a prefix:

```sh
cmake --install build --prefix /usr/local --strip
```

`--strip` drops the symbol table and any debug information from the installed binaries and plugins.
The installed prefix carries `bin/gman`, the three libraries and a CMake package. A program
links gman with `find_package(gman CONFIG REQUIRED)` and
`target_link_libraries(app PRIVATE gman::gman)`, and includes
`<gman/ri.h>`. It adds `gman::gman_ray` when it uses the ray geometry, the BVH, the emitters or the
indirect-light pass seam, and `gman::gman_raster` when it uses the body, face and vertex model, the
patch and polygon object manager or the polygon clipper. Link gman from C, below, walks through a
full example.

To remove what `cmake --install` wrote, delete the files CMake recorded:

```sh
xargs rm < build/install_manifest.txt
```

### Build with OpenEXR

OpenEXR is opt-in. Install OpenEXR 3.1 or a later 3.x (`libopenexr-dev` with apt, `openexr` with Homebrew)
and configure with it on:

```sh
cmake --preset dev -DGMAN_WITH_EXR=ON
```

Such a build writes `Display "x.exr"` as 32-bit float RGBA when `Quantize` is omitted or its `one` is
0, so a path-traced image keeps radiance above 1. Any other `one` stops the file at `WorldBegin` with
`RIE_INCAPABLE`, since EXR does not quantize. The build needs OpenEXR at run time, and release
tarballs carry no EXR driver.

OpenEXR output is beta. The driver writes linear floats correctly, but gman reads sRGB-encoded textures
and colours as linear, so an EXR looks washed out in a colour-managed viewer. Colour management
([#12](https://github.com/jac18281828/gman/issues/12)) will fix it.

### Development container

A devcontainer lives in `.devcontainer/`, carrying the same toolchain CI
uses: both gcc and clang, the sanitizers, valgrind, yamlfmt and commitlint.
Open the repo in VS Code and choose "Reopen in Container".

## Write a shader

A surface shader is a `GMANSurfaceShader` subclass, built as a loadable
module. Each `Surface` call builds its own instance, whose constructor
resolves its parameters from the plugin's `GMANParameterList`, so
`computeCi` and `computeOi` are `const`, read the `GMANSurfaceEnv` they are
given, and return a `GMANColor` by value. A shader may also override
`bsdf(GMANSurfaceEnv const&) const` to return the `gman::BSDF` the path
tracer samples, defaulting to a Lambert lobe of `Cs`; a `bsdf` override
must not call the base `albedo`, since the two would recurse. A shader may
override `albedo(GMANSurfaceEnv const&) const` to report its diffuse
reflectance, defaulting to its BSDF's `rhoD()` clamped to [0, 1]. A
closure combines libgman's own lobes — `addLambert`, `addGGX`, `addMirror`
and `addDielectric` — and never defines a new one; `rhoD` reports the
Lambert lobes alone, so a glossy or delta lobe never shows in `albedo`.
`shaders/gmanmatte.cpp` is the model:
it exports itself through three `extern "C"` entry points:

```cpp
extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }

extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(GMANParameterList const& parameters) {
  return new gmanshader::matte(parameters);
}

extern "C" GMAN_EXPORT void GMANDestroyShader(GMANShader* shader) { delete shader; }
```

Build it in-tree with `gman_add_plugin` in the root `CMakeLists.txt`:

```cmake
gman_add_plugin(gman_myshader shaders/gmanmyshader.cpp)
set_target_properties(gman_myshader PROPERTIES OUTPUT_NAME "myshader")
```

`Surface "myshader"` then loads `libmyshader.so`, the name `OUTPUT_NAME`
gives it. The installed package exports no `gman_add_plugin`, but it ships
`<gman/gmanshaderparams.h>`, whose helpers read a parameter by name, declared
or inline. An out-of-tree shader builds as a `MODULE` library linking
`gman::gman`, with prefix `lib` and suffix `.so`, as
`tests/install/CMakeLists.txt` does.

## Link gman from C

A C program calls the same RenderMan interface through `<gman/ri.h>`. This
one renders a lit sphere:

```c
#include <gman/ri.h>

int main(void) {
  RiBegin(RI_NULL);
  RiDisplay("sphere.tif", RI_FILE, RI_RGBA, RI_NULL);
  RiTranslate(0, 0, 5);

  RiWorldBegin();
  RiLightSource("ambientlight", RI_NULL);
  RiSphere(1, -1, 1, 360, RI_NULL);
  RiWorldEnd();

  RiEnd();
  return 0;
}
```

```cmake
cmake_minimum_required(VERSION 3.21)
project(sphere LANGUAGES C)

find_package(gman CONFIG REQUIRED)

add_executable(sphere sphere.c)
target_link_libraries(sphere PRIVATE gman::gman)
```

Configure against the installed prefix, build and run:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr/local
cmake --build build
./build/sphere
```

This writes `sphere.tif` in the current directory.

## The GMAN project

```
include/       libgman's header files, including ri.h
libgman/       the core library, gman: RIB parser, RI state machine, image writers
libgmanray/    gman_ray: ray geometry, BVH, emitters and the indirect-light pass seam
libgmanraster/ gman_raster: the body, face and vertex model, the polygon clipper
libgmanrib/    the RIB-writer plugin RiBegin("x.rib") loads, not yet trustworthy
renderers/     loadable rendering modules -- zbuffer, raytracer and pathtracer; radiosity,
               the radiosity pass the ray tracer loads
shaders/       loadable shading modules
gmansl/        grammar and driver for a shading language compiler that was never
               finished; kept as a record, built by nothing
gman/          the gman command line utility
samples/       demo scenes and their renders
tests/         the test suite and its RIB corpus
doc/           the 1999 design document
```

`tests/rib/` holds 242 more scenes, the test suite's own and its corpus.

## Related

- [The RenderMan Interface Specification 3.2](https://paulbourke.net/dataformats/rib/RISpec3_2.pdf),
  the RIB and C API gman implements.
- Other RenderMan-compatible renderers: Pixar's
  [RenderMan](https://renderman.pixar.com), [3Delight](https://www.3delight.com),
  [Aqsis](https://github.com/aqsis/aqsis) and [Pixie](https://sourceforge.net/projects/pixie/).
- [Physically Based Rendering](https://pbr-book.org), the reference for gman's path tracer, and
  [Veach's thesis](https://graphics.stanford.edu/papers/veach_thesis/), where multiple importance
  sampling comes from.
- [Open Shading Language](https://github.com/AcademySoftwareFoundation/OpenShadingLanguage), the
  shading language production renderers adopted after RenderMan's own.

## Contributing

Bug reports, fixes and scenes that render wrong are all welcome. For a rendering bug the `.rib`
file is the reproduction: attach it and the image you got.
[CONTRIBUTING.md](CONTRIBUTING.md) covers pull requests, the three checks to run before you push,
Conventional Commits and the rebase-only history. `AGENTS.md` briefs an AI agent working in the
tree.

## Benchmark

`bench/bench.sh` times the whole machine on `samples/materials.rib`. The script renders a queue of
independent copies, one per core at a time, and reports renders a minute. Build the release preset
first, which links with link-time optimization where the toolchain supports it (`-DGMAN_LTO=OFF`
turns it off), then run it:

```sh
cmake --preset release && cmake --build --preset release
bench/bench.sh
```

It prints the median of three runs and a pixel checksum that every render must share. Close heavy
programs first: a competing build slows a run by a third.

| Machine | Cores | Renders a minute |
|---|---|---|
| Apple M5 Max | 12 performance + 6 efficiency | 32 |
| Apple M3 Max | 12 performance + 4 efficiency | 28 |

Add a row for your machine with a pull request.
