# SPDX-License-Identifier: LGPL-2.1-or-later

# The phase 0 runtime baseline. Asserts what gman actually does with a minimal
# scene -- exit status, message and the absence of an image -- not what it was
# predicted to do. See tests/baseline_test.cpp for the finding and for which phase
# supersedes it.
add_executable(gman_baseline baseline_test.cpp)
target_link_libraries(gman_baseline PRIVATE gman_core TIFF::TIFF)

# Run in a scratch directory so the image the RIB names would land there, and
# so the assertion that none appears means something.
set(GMAN_BASELINE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/baseline-run")
file(MAKE_DIRECTORY "${GMAN_BASELINE_WORKDIR}")

add_test(NAME baseline
  COMMAND gman_baseline $<TARGET_FILE:gman>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere.rib"
  WORKING_DIRECTORY "${GMAN_BASELINE_WORKDIR}")

# gman dlopens lib<renderer>.so by bare name out of the build tree.
set_tests_properties(baseline PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# The RI state machine, through the C API. Replaces the shipped
# tests/basicstate.c, which could not work as written.
add_executable(gman_basicstate basicstate.c)
target_link_libraries(gman_basicstate PRIVATE gman_core)

# A C program against a C++ library has to be linked by the C++ driver. With
# the plain C driver clang omits the C++ runtimes, which is invisible in a
# normal build (gman_core's DT_NEEDED covers libstdc++) but fatal under the
# debug preset: -fsanitize=undefined includes the vptr check, whose runtime
# lives only in the C++ ubsan library, so the link fails on
# __ubsan_vptr_type_cache and __ubsan_handle_dynamic_type_cache_miss.
set_target_properties(gman_basicstate PROPERTIES LINKER_LANGUAGE CXX)

add_test(NAME basicstate
  COMMAND gman_basicstate
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(basicstate PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Phase 2 -- RIB dialect coverage. Each spawns the real gman binary, which
# dlopens its renderer by bare name, so each needs the same library path as
# the baseline test above.
set(GMAN_PHASE2_ENV
  "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}")

add_executable(gman_ribdialect ribdialect_test.cpp)
target_link_libraries(gman_ribdialect PRIVATE gman_core)

set(GMAN_RIBDIALECT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/ribdialect-run")
file(MAKE_DIRECTORY "${GMAN_RIBDIALECT_WORKDIR}")

add_test(NAME ribdialect
  COMMAND gman_ribdialect $<TARGET_FILE:gman>
                          "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_RIBDIALECT_WORKDIR}")
set_tests_properties(ribdialect PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 240)

add_executable(gman_readarchive readarchive_test.cpp)
target_link_libraries(gman_readarchive PRIVATE gman_core)

set(GMAN_READARCHIVE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/readarchive-run")
file(MAKE_DIRECTORY "${GMAN_READARCHIVE_WORKDIR}")

add_test(NAME readarchive
  COMMAND gman_readarchive $<TARGET_FILE:gman>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib/archive"
  WORKING_DIRECTORY "${GMAN_READARCHIVE_WORKDIR}")
set_tests_properties(readarchive PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 60)

add_executable(gman_gzip gzip_test.cpp)
target_link_libraries(gman_gzip PRIVATE gman_core)

set(GMAN_GZIP_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/gzip-run")
file(MAKE_DIRECTORY "${GMAN_GZIP_WORKDIR}")

add_test(NAME gzip
  COMMAND gman_gzip $<TARGET_FILE:gman>
                    "${CMAKE_CURRENT_SOURCE_DIR}/rib/gzip"
                    "${GMAN_GZIP_WORKDIR}/scratch"
  WORKING_DIRECTORY "${GMAN_GZIP_WORKDIR}")
set_tests_properties(gzip PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

add_executable(gman_facevarying facevarying_test.cpp)
target_link_libraries(gman_facevarying PRIVATE gman_core)

add_test(NAME facevarying COMMAND gman_facevarying)
set_tests_properties(facevarying PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Phase 1 -- the object -> world -> camera -> screen -> NDC -> raster chain.
add_executable(gman_matrix4 gmanmatrix4_test.cpp)
target_link_libraries(gman_matrix4 PRIVATE gman_core)
add_test(NAME matrix4 COMMAND gman_matrix4)
set_tests_properties(matrix4 PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_spacechain spacechain_test.cpp)
target_link_libraries(gman_spacechain PRIVATE gman_core)
add_test(NAME spacechain COMMAND gman_spacechain)
set_tests_properties(spacechain PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Phase 3 -- analytic primitive normals: finite-difference agreement,
# cross-primitive orientation consistency, inverse-transpose under a
# non-uniform scale.
add_executable(gman_normals normals_test.cpp)
target_link_libraries(gman_normals PRIVATE gman_core)
add_test(NAME normals COMMAND gman_normals)
# The inverse-transpose proof (item 3) tessellates a real sphere through
# GMANPatchPolyObjectManager, which falls back to dlopen'ing "libmatte.so"
# when no RiSurface is set -- the same bare-name lookup gman itself relies
# on the build tree being on the runtime search path for.
set_tests_properties(normals PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The phase's actual gate: a real end-to-end render, read back and measured
# against the hand-derived silhouette geometry.
add_executable(gman_silhouette silhouette_test.cpp)
target_link_libraries(gman_silhouette PRIVATE gman_core TIFF::TIFF)

set(GMAN_SILHOUETTE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/silhouette-run")
file(MAKE_DIRECTORY "${GMAN_SILHOUETTE_WORKDIR}")

add_test(NAME silhouette
  COMMAND gman_silhouette $<TARGET_FILE:gman>
  WORKING_DIRECTORY "${GMAN_SILHOUETTE_WORKDIR}")
set_tests_properties(silhouette PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# An in-world Rotate must compose ahead of the transforms declared after it,
# about the world origin, not the camera (GMANGraphicState::buildTransform).
add_executable(gman_rotate rotate_test.cpp)
target_link_libraries(gman_rotate PRIVATE gman_core TIFF::TIFF)

set(GMAN_ROTATE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/rotate-run")
file(MAKE_DIRECTORY "${GMAN_ROTATE_WORKDIR}")

add_test(NAME rotate
  COMMAND gman_rotate $<TARGET_FILE:gman>
                      "${CMAKE_CURRENT_SOURCE_DIR}/rib/rotate.rib"
  WORKING_DIRECTORY "${GMAN_ROTATE_WORKDIR}")
set_tests_properties(rotate PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Proof item 6 (secondary, visual): the same sphere at several RiTranslate
# offsets.
add_executable(gman_transforms transforms_test.cpp)
target_link_libraries(gman_transforms PRIVATE gman_core TIFF::TIFF)

set(GMAN_TRANSFORMS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/transforms-run")
file(MAKE_DIRECTORY "${GMAN_TRANSFORMS_WORKDIR}")

add_test(NAME transforms
  COMMAND gman_transforms $<TARGET_FILE:gman>
                          "${CMAKE_CURRENT_SOURCE_DIR}/rib/transforms.rib"
  WORKING_DIRECTORY "${GMAN_TRANSFORMS_WORKDIR}")
set_tests_properties(transforms PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Proof items 4-6: the N.L=0 terminator, RiSides 1 vs RiSides 2, and the
# golden image (tests/rib/lights.rib against tests/rib/lights_golden.tif).
add_executable(gman_lighting lighting_test.cpp)
target_link_libraries(gman_lighting PRIVATE gman_core TIFF::TIFF)

set(GMAN_LIGHTING_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/lighting-run")
file(MAKE_DIRECTORY "${GMAN_LIGHTING_WORKDIR}")

add_test(NAME lighting
  COMMAND gman_lighting $<TARGET_FILE:gman>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_LIGHTING_WORKDIR}")
set_tests_properties(lighting PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Step 3/5: all seven quadric primitives in one scene, golden-image
# regression via the shared harness (tests/goldenimage.h).
add_executable(gman_quadrics quadrics_test.cpp)
target_link_libraries(gman_quadrics PRIVATE gman_core TIFF::TIFF)

set(GMAN_QUADRICS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/quadrics-run")
file(MAKE_DIRECTORY "${GMAN_QUADRICS_WORKDIR}")

add_test(NAME quadrics
  COMMAND gman_quadrics $<TARGET_FILE:gman>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_QUADRICS_WORKDIR}")
set_tests_properties(quadrics PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Step 3/5: RiScreenWindow + RiCropWindow together under perspective, plus
# an unrecognized RiFrameAspectRatio proving the skip path does not desync.
add_executable(gman_screenwindow screenwindow_test.cpp)
target_link_libraries(gman_screenwindow PRIVATE gman_core TIFF::TIFF)

set(GMAN_SCREENWINDOW_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/screenwindow-run")
file(MAKE_DIRECTORY "${GMAN_SCREENWINDOW_WORKDIR}")

add_test(NAME screenwindow
  COMMAND gman_screenwindow $<TARGET_FILE:gman>
                            "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_SCREENWINDOW_WORKDIR}")
set_tests_properties(screenwindow PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Step 3/5: a cylinder straddling both the near and the far clip plane.
add_executable(gman_clipping clipping_test.cpp)
target_link_libraries(gman_clipping PRIVATE gman_core TIFF::TIFF)

set(GMAN_CLIPPING_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/clipping-run")
file(MAKE_DIRECTORY "${GMAN_CLIPPING_WORKDIR}")

add_test(NAME clipping
  COMMAND gman_clipping $<TARGET_FILE:gman>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_CLIPPING_WORKDIR}")
set_tests_properties(clipping PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Step 3/5: matte, plastic and metal side by side under identical lighting.
add_executable(gman_shaders shaders_test.cpp)
target_link_libraries(gman_shaders PRIVATE gman_core TIFF::TIFF)

set(GMAN_SHADERS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/shaders-run")
file(MAKE_DIRECTORY "${GMAN_SHADERS_WORKDIR}")

add_test(NAME shaders
  COMMAND gman_shaders $<TARGET_FILE:gman>
                       "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_SHADERS_WORKDIR}")
set_tests_properties(shaders PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# The per-pixel quantize log spam defect: GMANQuantize::doColor must warn
# at most once per process, not once per pixel.
add_executable(gman_quantize quantize_test.cpp)
target_link_libraries(gman_quantize PRIVATE gman_core)

set(GMAN_QUANTIZE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/quantize-run")
file(MAKE_DIRECTORY "${GMAN_QUANTIZE_WORKDIR}")

add_test(NAME quantize
  COMMAND gman_quantize $<TARGET_FILE:gman>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_QUANTIZE_WORKDIR}")
set_tests_properties(quantize PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Step 4: RIB tokenizer robustness on malformed input -- an unterminated
# string, an unbalanced bracket, a file truncated mid-keyword -- each
# under its own hard timeout (see tests/ribmalformed_test.cpp).
add_executable(gman_ribmalformed ribmalformed_test.cpp)
target_link_libraries(gman_ribmalformed PRIVATE gman_core)

add_test(NAME ribmalformed
  COMMAND gman_ribmalformed $<TARGET_FILE:gman>
                            "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed")
set_tests_properties(ribmalformed PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# Bilinear and bicubic Patch rasterization (SPEC.md Section 8, phase 6).
# See tests/patchnorender_test.cpp.
add_executable(gman_patchnorender patchnorender_test.cpp)
target_link_libraries(gman_patchnorender PRIVATE gman_core TIFF::TIFF)

set(GMAN_PATCHNORENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/patchnorender-run")
file(MAKE_DIRECTORY "${GMAN_PATCHNORENDER_WORKDIR}")

add_test(NAME patchnorender
  COMMAND gman_patchnorender $<TARGET_FILE:gman>
                             "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_PATCHNORENDER_WORKDIR}")
set_tests_properties(patchnorender PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Review finding: no test rendered a genuinely asymmetric partial sphere's
# image (transforms.rib's Sphere 0.5 -0.5 0.5 360 is a full sphere). This
# is the missing end-to-end proof of GMANSphere::getLocation's zmin/zmax
# fix, and of the Clipping workaround the near-clip precision defect
# requires for this shape.
add_executable(gman_partialsphere partialsphere_test.cpp)
target_link_libraries(gman_partialsphere PRIVATE gman_core TIFF::TIFF)

set(GMAN_PARTIALSPHERE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/partialsphere-run")
file(MAKE_DIRECTORY "${GMAN_PARTIALSPHERE_WORKDIR}")

add_test(NAME partialsphere
  COMMAND gman_partialsphere $<TARGET_FILE:gman>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_PARTIALSPHERE_WORKDIR}")
set_tests_properties(partialsphere PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Review finding: GMANVertex::calcNormal has no call site anywhere in this
# tree (setFaceList is never called), so the phase-3 implementation of it
# never executes. Direct unit test of its arithmetic, independent of any
# tessellator.
add_executable(gman_vertexnormal vertexnormal_test.cpp)
target_link_libraries(gman_vertexnormal PRIVATE gman_core)
add_test(NAME vertexnormal COMMAND gman_vertexnormal)
set_tests_properties(vertexnormal PROPERTIES
  LABELS unit
  TIMEOUT 30)

# RISpec pixel-reconstruction filters (box, triangle, gaussian,
# catmull-rom, sinc): a pure unit test on gmanfilters.cpp, no gman binary.
# See tests/filters_test.cpp for which of the five were wrong and why.
add_executable(gman_filters filters_test.cpp)
target_link_libraries(gman_filters PRIVATE gman_core)
add_test(NAME filters COMMAND gman_filters)
set_tests_properties(filters PROPERTIES
  LABELS unit
  TIMEOUT 30)

# AGENTS.md's Gates block against .github/workflows/ci.yml -- a docs
# rewrite has no other fails-when-reverted mechanism. Reads the checked-in
# source tree directly; no dependency on this test's own build output.
add_executable(gman_docsconsistency docsconsistency_test.cpp)
add_test(NAME docsconsistency
  COMMAND gman_docsconsistency
          "${CMAKE_SOURCE_DIR}/AGENTS.md"
          "${CMAKE_SOURCE_DIR}/.github/workflows/ci.yml"
          "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
set_tests_properties(docsconsistency PROPERTIES
  LABELS unit
  TIMEOUT 30)

# UAT defect 0: a Projection with no explicit "fov" threw instead of
# defaulting to 90 per RISpec 3.2.
add_executable(gman_fovdefault fovdefault_test.cpp)
target_link_libraries(gman_fovdefault PRIVATE gman_core TIFF::TIFF)

set(GMAN_FOVDEFAULT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/fovdefault-run")
file(MAKE_DIRECTORY "${GMAN_FOVDEFAULT_WORKDIR}")

add_test(NAME fovdefault
  COMMAND gman_fovdefault $<TARGET_FILE:gman>
  WORKING_DIRECTORY "${GMAN_FOVDEFAULT_WORKDIR}")
set_tests_properties(fovdefault PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# UAT defect 1: Surface before AttributeBegin/FrameBegin double-freed a
# dlopen'd shader module on the attributesStack copy both requests make.
add_executable(gman_attributescopy attributescopy_test.cpp)
target_link_libraries(gman_attributescopy PRIVATE gman_core)

set(GMAN_ATTRIBUTESCOPY_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/attributescopy-run")
file(MAKE_DIRECTORY "${GMAN_ATTRIBUTESCOPY_WORKDIR}")

add_test(NAME attributescopy
  COMMAND gman_attributescopy $<TARGET_FILE:gman>
  WORKING_DIRECTORY "${GMAN_ATTRIBUTESCOPY_WORKDIR}")
set_tests_properties(attributescopy PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# UAT defect 2: the PNG output driver segfaulted -- a read-side libpng call
# on a write struct, a mis-sized pixel buffer, and a loop that only ever
# wrote column 0.
add_executable(gman_pngdriver pngdriver_test.cpp)
target_link_libraries(gman_pngdriver PRIVATE gman_core PNG::PNG TIFF::TIFF)

set(GMAN_PNGDRIVER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/pngdriver-run")
file(MAKE_DIRECTORY "${GMAN_PNGDRIVER_WORKDIR}")

add_test(NAME pngdriver
  COMMAND gman_pngdriver $<TARGET_FILE:gman>
  WORKING_DIRECTORY "${GMAN_PNGDRIVER_WORKDIR}")
set_tests_properties(pngdriver PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# UAT defect 3: .jpg/.jpeg had no Display extension dispatch branch, so
# newOutput stayed null and RiWorldEnd dereferenced it unconditionally. The
# binary needs libjpeg to link, so it stays guarded; the test itself
# registers unconditionally so a build without libjpeg reports the gap as
# Skipped rather than making the JPEG driver invisible to ctest.
if(GMAN_WITH_JPEG)
  add_executable(gman_jpegdriver jpegdriver_test.cpp)
  target_link_libraries(gman_jpegdriver PRIVATE gman_core JPEG::JPEG TIFF::TIFF)

  set(GMAN_JPEGDRIVER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/jpegdriver-run")
  file(MAKE_DIRECTORY "${GMAN_JPEGDRIVER_WORKDIR}")

  set(GMAN_JPEGDRIVER_COMMAND gman_jpegdriver $<TARGET_FILE:gman>)
else()
  set(GMAN_JPEGDRIVER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}")
  set(GMAN_JPEGDRIVER_COMMAND sh -c "exit 77")
endif()

add_test(NAME jpegdriver
  COMMAND ${GMAN_JPEGDRIVER_COMMAND}
  WORKING_DIRECTORY "${GMAN_JPEGDRIVER_WORKDIR}")
set_tests_properties(jpegdriver PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30
  SKIP_RETURN_CODE 77)

# UAT defect 4: CropWindow truncated from the origin instead of cropping --
# nothing offset rasterization by the crop rectangle's own origin.
add_executable(gman_cropwindow cropwindow_test.cpp)
target_link_libraries(gman_cropwindow PRIVATE gman_core TIFF::TIFF)

set(GMAN_CROPWINDOW_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/cropwindow-run")
file(MAKE_DIRECTORY "${GMAN_CROPWINDOW_WORKDIR}")

add_test(NAME cropwindow
  COMMAND gman_cropwindow $<TARGET_FILE:gman>
  WORKING_DIRECTORY "${GMAN_CROPWINDOW_WORKDIR}")
set_tests_properties(cropwindow PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# UAT defect 5: orthographic ignored RiScreenWindow and hard-clipped at
# camera-space x=+-1. GMANPolygonClipper::clip now rebuilds its side planes
# from the screen window for both projections -- see the perspective
# analogue of this defect in gmanpolygonclipper.cpp's own history.
add_executable(gman_orthowindow orthowindow_test.cpp)
target_link_libraries(gman_orthowindow PRIVATE gman_core TIFF::TIFF)

set(GMAN_ORTHOWINDOW_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/orthowindow-run")
file(MAKE_DIRECTORY "${GMAN_ORTHOWINDOW_WORKDIR}")

add_test(NAME orthowindow
  COMMAND gman_orthowindow $<TARGET_FILE:gman>
  WORKING_DIRECTORY "${GMAN_ORTHOWINDOW_WORKDIR}")
set_tests_properties(orthowindow PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Phase 5's actual gate: tests/rib/polygon.rib renders a convex pentagon
# whose raster coverage and shaded colour both match an analytically
# computed region and value.
add_executable(gman_polygon polygon_test.cpp)
target_link_libraries(gman_polygon PRIVATE gman_core TIFF::TIFF)

set(GMAN_POLYGON_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/polygon-run")
file(MAKE_DIRECTORY "${GMAN_POLYGON_WORKDIR}")

add_test(NAME polygon
  COMMAND gman_polygon $<TARGET_FILE:gman>
                       "${CMAKE_CURRENT_SOURCE_DIR}/rib/polygon.rib"
  WORKING_DIRECTORY "${GMAN_POLYGON_WORKDIR}")
set_tests_properties(polygon PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# 0.7: the sample buffer and pixel filtering. testSupersamplingEdge is the
# falsification proof (1x1 vs 4x4 PixelSamples on the same hard edge);
# testPixelFilterDiffers needs RiPixelFilter's RIB wiring to be real.
add_executable(gman_samplebuffer samplebuffer_test.cpp)
target_link_libraries(gman_samplebuffer PRIVATE gman_core TIFF::TIFF)

set(GMAN_SAMPLEBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/samplebuffer-run")
file(MAKE_DIRECTORY "${GMAN_SAMPLEBUFFER_WORKDIR}")

add_test(NAME samplebuffer
  COMMAND gman_samplebuffer $<TARGET_FILE:gman>
  WORKING_DIRECTORY "${GMAN_SAMPLEBUFFER_WORKDIR}")
set_tests_properties(samplebuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Perspective clipping honours RiScreenWindow, not the canonical +-1 NDC
# cube: tests/rib/wideframe.rib's rectangle spans a wide, non-square
# frame's default aspect-derived window.
add_executable(gman_wideframe wideframe_test.cpp)
target_link_libraries(gman_wideframe PRIVATE gman_core TIFF::TIFF)

set(GMAN_WIDEFRAME_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/wideframe-run")
file(MAKE_DIRECTORY "${GMAN_WIDEFRAME_WORKDIR}")

add_test(NAME wideframe
  COMMAND gman_wideframe $<TARGET_FILE:gman>
                         "${CMAKE_CURRENT_SOURCE_DIR}/rib/wideframe.rib"
  WORKING_DIRECTORY "${GMAN_WIDEFRAME_WORKDIR}")
set_tests_properties(wideframe PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)
