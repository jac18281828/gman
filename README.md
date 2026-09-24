# GMAN — a RenderMan-compatible renderer

[![ci](https://github.com/jac18281828/gman/actions/workflows/ci.yml/badge.svg)](https://github.com/jac18281828/gman/actions/workflows/ci.yml)

GMAN is a 1999 RenderMan renderer revived and rewritten to build again in
2026. Point it at an `.rib` file and it renders an image, through a
z-buffer renderer or a ray tracer. C++20 and CMake, with C modules
throughout.

![A robot drives into a table; the translucent vase tips and its flowers eject](samples/vase-raytraced.png)

*The robot crashes into the table; the vase, now translucent, tips and its
flowers eject* — `samples/vase.rib`, ray-traced. Quadrics and polygons, two
surface shaders and three lights.

## Install

Binary releases ship for Linux x86_64, Linux arm64 and macOS arm64. Download
the tarball for your platform from the
[releases page](https://github.com/jac18281828/gman/releases) and extract
it:

```sh
tar xzf gman-0.9.0-macos-arm64.tar.gz
```

It unpacks `bin/gman`, the renderer and shader plugins under `lib/`, and
`include/gman`. It carries no scenes: clone the repo, or download
`samples/` from it, to render the examples below.

### Building from source

Requires CMake 3.21 or newer, a C++20 compiler with `<format>` (GCC 13 or
Clang 17 or newer), libtiff and zlib. libpng and libjpeg are optional: a
build without one rejects that `Display` extension with `RIE_BADFILE`, and
`gman --version` lists the drivers actually compiled in. POSIX only —
macOS and Linux.

```sh
cmake --preset dev
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`AGENTS.md`'s Tests section covers the test layout, adding a new test, and
golden-image regeneration; its Gates section is the full gate list CI runs.

To install into a prefix:

```sh
cmake --install build --prefix /usr/local
```

The installed prefix carries `bin/gman` and a CMake package. A program
links gman with `find_package(gman CONFIG REQUIRED)` and
`target_link_libraries(app PRIVATE gman::gman_core)`, and includes
`<gman/ri.h>` — Link libgman from C, below, walks through a full example.

#### Development container

A devcontainer carrying the same toolchain CI uses — both gcc and clang, the
sanitizers, valgrind, yamlfmt and commitlint — lives in `.devcontainer/`.
Open the repo in VS Code and choose "Reopen in Container", or run every gate
at once with:

```sh
./build.sh
```

## Render

`gman scene.rib` renders a RIB file through the default z-buffer renderer,
`gmanzbuffer`. `-r gmanraytracer` renders the same file through the ray
tracer instead, with real reflection, refraction and shadows. Output lands
wherever the scene's own `Display` request names, relative to the current
directory — `gman` takes no output flag of its own.

`-d`, `-i`, `-w`, `-e` and `-q` set the log level to debug, info (the
default), warning, error or disaster-only; `-l` also writes a log file named
for the RIB; `-h` prints usage. `--version` prints the version and the
drivers built in, then exits:

```sh
gman --version
```

```
gman 0.9.1
drivers: tiff pnm png jpeg
```

At the default `Clipping`, flat or narrow-z-range geometry can render
corrupted or blank; pair it with an explicit `Clipping <near> <far>`.

## Scenes

`samples/vase.rib` is a room, a table, a vase of flowers and a robot mid
crash: quadrics and polygons, the `matte` and `plastic` surface shaders,
three lights. Its `Display` writes PNG, which needs libpng at build time —
without it, `gman samples/vase.rib` rejects the scene with `RIE_BADFILE`.

Render it through the default z-buffer renderer:

```sh
cd samples
gman vase.rib
cd ..
```

Both renderers write the same `Display` name, `vase.png`, so render the ray
tracer's version in its own directory and rename it clear:

```sh
mkdir vase-raytraced
cd vase-raytraced
gman -r gmanraytracer ../samples/vase.rib
mv vase.png ../samples/vase-raytraced.png
cd ..
```

The ray-traced render is this README's hero image, above; `samples/vase.png`
is the fast preview.

A few more scenes worth running, from `tests/rib/`:

```sh
cd tests/rib
gman sphere.rib
gman shaders.rib
gman -r gmanraytracer r8_mirror.rib
cd ..
```

- `sphere.rib` — one sphere, the renderer's own runtime baseline.
- `shaders.rib` — `matte`, `plastic` and `metal` side by side under
  identical lighting.
- `r8_mirror.rib` — a mirrored sphere. Only `-r gmanraytracer` traces its
  reflection; `trace()` returns black under the z-buffer.

## Write a shader

A surface shader is a `GMANSurfaceShader` subclass, built as a loadable
module. `shaders/gmanmatte.cpp` is the model: it computes `Ci` and `Oi` from
a `GMANSurfaceEnv`, and exports itself through two `extern "C"` entry
points:

```cpp
extern "C" GMAN_EXPORT GMANLoadableObjectInfo* GMANGetLoadableInfo(void) { return &loadableInfo; }
extern "C" GMAN_EXPORT GMANShader* GMANLoadShader(void) { return &shader; }
```

Build it in-tree with `gman_add_plugin` in the root `CMakeLists.txt`:

```cmake
gman_add_plugin(gman_myshader shaders/gmanmyshader.cpp)
set_target_properties(gman_myshader PROPERTIES OUTPUT_NAME "myshader")
```

`Surface "myshader"` then loads `libmyshader.so`, the name `OUTPUT_NAME`
gives it. The installed package exports no `gman_add_plugin`: a new shader
builds only inside a gman checkout.

## Link libgman from C

```c
#include <gman/ri.h>

int main(void) {
  RiBegin(RI_NULL);
  RiDisplay("sphere.tif", RI_FILE, RI_RGBA, RI_NULL);

  RiWorldBegin();
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
target_link_libraries(sphere PRIVATE gman::gman_core)
```

Configure against the installed prefix, build and run:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr/local
cmake --build build
./build/sphere
```

This writes `sphere.tif` in the current directory.

## The tree

```
include/     GMAN header files, including ri.h
libgman/     the core library: RIB parser, RI state machine, image writers
libgmanrib/  the RIB-writer plugin RiBegin("x.rib") loads, not yet trustworthy
renderers/   loadable rendering modules -- zbuffer and raytracer, plus reyes and
             radiosity, which are non-functional and build OFF by default
shaders/     loadable shading modules
gmansl/      grammar and driver for a shading language compiler that was never
             finished; kept as a record, built by nothing
gman/        the gman command line utility
samples/     demo scenes and their renders
tests/       the test suite and its RIB corpus
doc/         the 1999 design document
```

## Against the standard

What the RenderMan standard asks of a renderer, and where GMAN stands on
each:

- **[ ] High-end geometry.** NURBS, trim curves and subdivision surfaces
  parse and are ignored; `Patch`, `PatchMesh`, `NuPatch` and the two
  `PointsPolygons` requests all rasterize, faceted or bilinear/bicubic.
- **[~] Antialiasing and motion blur.** `PixelSamples` and `PixelFilter`
  resolve through five filter kernels; motion blur is absent, `Shutter` and
  `DepthOfField` read and unused.
- **[~] Programmable shading.** Surface and light shaders are C++ modules
  loaded at run time — pluggable, not programmable; volume shaders parse and
  do nothing.
- **[ ] Displacement shading.** Wants micropolygons, which want REYES.
- **[~] Many large textures, flat memory.** `texture()` and `environment()`
  read through an in-memory cache that decodes each name once and never
  bounds its own memory.
- **[~] Quantization, filtering, reconstruction.** Exposure, gamma and pixel
  reconstruction run; quantization still warns and passes colour through
  untouched.
- **[~] Shading time against shading quality.** The z-buffer renderer's
  polygon dicing sizes each facet to `ShadingRate` and its raster extent;
  quadrics, patches and the detail controls still ignore it.

None finished, five begun.

## Files

```
COPYING      GNU Lesser General Public License, version 2.1
AGENTS.md    build commands, gate list and house conventions
AUTHORS      contributors
```
