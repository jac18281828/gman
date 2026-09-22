# SPDX-License-Identifier: LGPL-2.1-or-later

# A test that includes a libgman/ header links this instead of adding
# libgman/ to its own include path.
add_library(gman_internal_headers INTERFACE)
target_include_directories(gman_internal_headers INTERFACE
  "${CMAKE_SOURCE_DIR}/libgman")

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

# RiBegin("x.rib") loads libgmanrib.so and writes RIB through GMANASCII;
# RI_NULL still routes to the renderer. See tests/ribwriter.c.
add_executable(gman_ribwriter ribwriter.c)
target_link_libraries(gman_ribwriter PRIVATE gman_core)
set_target_properties(gman_ribwriter PROPERTIES LINKER_LANGUAGE CXX)
# gman_ribwriter dlopens libgmanrib.so at runtime rather than linking it, so
# nothing else forces the plugin to build before the test does.
add_dependencies(gman_ribwriter gman_rib)

set(GMAN_RIBWRITER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/ribwriter-run")
file(MAKE_DIRECTORY "${GMAN_RIBWRITER_WORKDIR}")

add_test(NAME ribwriter
  COMMAND gman_ribwriter
          "${GMAN_RIBWRITER_WORKDIR}/scene.rib"
          "${CMAKE_CURRENT_SOURCE_DIR}/rib/golden/ribwriter_scene.rib"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(ribwriter PROPERTIES
  LABELS unit
  TIMEOUT 30
  RESOURCE_LOCK gmanrib_plugin_file)

# A failed .rib load leaves no context current. See tests/ribwriterfailure.c,
# which renames libgmanrib.so aside for the run and restores it whether it
# passes or fails. libgmanrib.so is also read by gman_ribwriter and by the
# install test (which cmake --installs it), so every test below that touches
# it carries RESOURCE_LOCK gmanrib_plugin_file, keeping ctest from ever
# running two of them at once -- including two repeats of this test under
# --repeat, which would otherwise race each other's rename.
#
# ribwriterfailure_cleanup is paired to ribwriterfailure through a fixture
# rather than a plain dependency so ctest runs it even if ribwriterfailure
# crashes or times out before its own restore -- the case its own restore
# can't cover. FIXTURES_SETUP here does not mean another test requires the
# fixture; it exists only to give ribwriterfailure a paired
# FIXTURES_CLEANUP test.
add_executable(gman_ribwriterfailure ribwriterfailure.c)
target_link_libraries(gman_ribwriterfailure PRIVATE gman_core)
set_target_properties(gman_ribwriterfailure PROPERTIES LINKER_LANGUAGE CXX)
add_dependencies(gman_ribwriterfailure gman_rib)

add_test(NAME ribwriterfailure
  COMMAND gman_ribwriterfailure "$<TARGET_FILE:gman_rib>")
set_tests_properties(ribwriterfailure PROPERTIES
  LABELS unit
  TIMEOUT 30
  RESOURCE_LOCK gmanrib_plugin_file
  FIXTURES_SETUP gmanrib_hidden)

add_test(NAME ribwriterfailure_cleanup
  COMMAND "${CMAKE_COMMAND}" -D "PLUGIN=$<TARGET_FILE:gman_rib>"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/restoreplugin.cmake")
set_tests_properties(ribwriterfailure_cleanup PROPERTIES
  LABELS unit
  TIMEOUT 30
  RESOURCE_LOCK gmanrib_plugin_file
  FIXTURES_CLEANUP gmanrib_hidden)

# Two RiBegin/RiEnd cycles with a parameterized surface shader, plus process
# exit: the three points where a dlopen'd, never-unloaded shader's static
# instance is reached again. See tests/shaderoutlivesrenderer_test.c.
add_executable(gman_shaderoutlivesrenderer shaderoutlivesrenderer_test.c)
target_link_libraries(gman_shaderoutlivesrenderer PRIVATE gman_core)
# RiBegin(RI_NULL) dlopens the default renderer; RiSurface dlopens plastic.
add_dependencies(gman_shaderoutlivesrenderer gman_zbuffer gman_plastic)

# Same C-against-C++ link requirement as gman_basicstate above.
set_target_properties(gman_shaderoutlivesrenderer PROPERTIES LINKER_LANGUAGE CXX)

add_test(NAME shaderoutlivesrenderer
  COMMAND gman_shaderoutlivesrenderer
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# RiSurface dlopens libplastic.so by bare name out of the build tree.
set_tests_properties(shaderoutlivesrenderer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANParameterList's own contract, in-process: default/moved-from/copy
# lifetime and throw-safety. See tests/paramlist_test.cpp.
add_executable(gman_paramlist paramlist_test.cpp)
target_link_libraries(gman_paramlist PRIVATE gman_core)

add_test(NAME paramlist COMMAND gman_paramlist)
set_tests_properties(paramlist PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANDictionary::getTypeSize's default: arm reports an unknown token type
# truthfully. See tests/dictionaryerror_test.cpp.
add_executable(gman_dictionaryerror dictionaryerror_test.cpp)
target_link_libraries(gman_dictionaryerror PRIVATE gman_core)

add_test(NAME dictionaryerror COMMAND gman_dictionaryerror)
set_tests_properties(dictionaryerror PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Pins RIE_NOTSTARTED's and RiDeclare's RIE_SYNTAX messages through
# RiErrorHandler.
add_executable(gman_diagnostics diagnostics_test.cpp)
target_link_libraries(gman_diagnostics PRIVATE gman_core)

add_test(NAME diagnostics
  COMMAND gman_diagnostics
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(diagnostics PROPERTIES
  LABELS unit
  TIMEOUT 30)

# A world block with no display output -- no Display, or one naming
# "framebuffer" -- reports RIE_ILLSTATE instead of rendering into
# gman::OutputX11's 0x0 stub. See tests/worldnodisplay_test.cpp.
add_executable(gman_worldnodisplay worldnodisplay_test.cpp)
target_link_libraries(gman_worldnodisplay PRIVATE gman_core)
add_dependencies(gman_worldnodisplay gman_zbuffer)

add_test(NAME worldnodisplay_zbuffer
  COMMAND gman_worldnodisplay gmanzbuffer
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(worldnodisplay_zbuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_worldnodisplay gman_raytracer)

  add_test(NAME worldnodisplay_raytracer
    COMMAND gman_worldnodisplay gmanraytracer
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
  set_tests_properties(worldnodisplay_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS unit
    TIMEOUT 30)
endif()

# GMANRenderManImpl's renderer/objectManager/worldManager/viewingSystem/output
# pointers stay null from construction, and RiEnd leaves null every one it
# releases. See tests/rendermanpointers_test.cpp.
add_executable(gman_rendermanpointers rendermanpointers_test.cpp)
target_link_libraries(gman_rendermanpointers PRIVATE gman_core)
add_dependencies(gman_rendermanpointers gman_zbuffer)

add_test(NAME rendermanpointers
  COMMAND gman_rendermanpointers
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(rendermanpointers PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
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

add_executable(gman_logfilename logfilename_test.cpp)
target_link_libraries(gman_logfilename PRIVATE gman_core)

set(GMAN_LOGFILENAME_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/logfilename-run")
file(MAKE_DIRECTORY "${GMAN_LOGFILENAME_WORKDIR}")

add_test(NAME logfilename
  COMMAND gman_logfilename $<TARGET_FILE:gman>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere.rib"
                           "${GMAN_LOGFILENAME_WORKDIR}/scratch"
  WORKING_DIRECTORY "${GMAN_LOGFILENAME_WORKDIR}")
set_tests_properties(logfilename PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

add_executable(gman_rflag rflag_test.cpp)
target_link_libraries(gman_rflag PRIVATE gman_core)

set(GMAN_RFLAG_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/rflag-run")
file(MAKE_DIRECTORY "${GMAN_RFLAG_WORKDIR}")

add_test(NAME rflag
  COMMAND gman_rflag $<TARGET_FILE:gman>
                     "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere.rib"
  WORKING_DIRECTORY "${GMAN_RFLAG_WORKDIR}")
set_tests_properties(rflag PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

add_executable(gman_facevarying facevarying_test.cpp)
target_link_libraries(gman_facevarying PRIVATE gman_core)

add_test(NAME facevarying COMMAND gman_facevarying)
set_tests_properties(facevarying PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_bitmap bitmap_test.cpp)
target_link_libraries(gman_bitmap PRIVATE gman_core)

add_test(NAME bitmap COMMAND gman_bitmap)
set_tests_properties(bitmap PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_inlineparsewordbound inlineparsewordbound_test.cpp)
target_link_libraries(gman_inlineparsewordbound PRIVATE gman_core gman_internal_headers)

add_test(NAME inlineparsewordbound COMMAND gman_inlineparsewordbound)
set_tests_properties(inlineparsewordbound PROPERTIES
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
target_link_libraries(gman_spacechain PRIVATE gman_core gman_internal_headers)
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

# Scale- and placement-invariance of GMANPatchPolyObjectManager::getRSPolygon's
# ear-clipping classification: nine hand-built rings at six scales in two
# placements, a randomized star-shaped stress, and degenerate input.
add_executable(gman_triangulation triangulation_test.cpp)
target_link_libraries(gman_triangulation PRIVATE gman_core)
add_test(NAME triangulation COMMAND gman_triangulation)
# getRSPolygon falls back to dlopen'ing "libmatte.so" when no RiSurface is
# set, the same bare-name lookup gman itself relies on the build tree being
# on the runtime search path for.
set_tests_properties(triangulation PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GeneralPolygon hole bridging: getRSGeneralPolygon merges each hole into
# the outer loop by Eberly's method before handing the ring to the same
# triangulateEarClipping loop 1 already uses. See tests/generalpolygon_test.cpp.
add_executable(gman_generalpolygon generalpolygon_test.cpp)
target_link_libraries(gman_generalpolygon PRIVATE gman_core)
add_test(NAME generalpolygon COMMAND gman_generalpolygon)
# getRSGeneralPolygon falls back to dlopen'ing "libmatte.so" when no
# RiSurface is set, the same bare-name lookup gman itself relies on the
# build tree being on the runtime search path for.
set_tests_properties(generalpolygon PROPERTIES
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

# GMAN_LIGHT_SPOT: tests/rib/spotlight.rib puts the light on a disk's own
# axis, so a point inside coneangle is lit, a point past it is not, and
# the boundary between them tapers over conedeltaangle rather than
# stepping. tests/rib/spotlight_beam.rib is the same scene at a lower
# intensity, isolating beamdistribution's own falloff from that taper and
# from exposure saturation; gman_spotlight renders and checks both.
add_executable(gman_spotlight spotlight_test.cpp)
target_link_libraries(gman_spotlight PRIVATE gman_core TIFF::TIFF)

set(GMAN_SPOTLIGHT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/spotlight-run")
file(MAKE_DIRECTORY "${GMAN_SPOTLIGHT_WORKDIR}")

add_test(NAME spotlight
  COMMAND gman_spotlight $<TARGET_FILE:gman>
                         "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_SPOTLIGHT_WORKDIR}")
set_tests_properties(spotlight PROPERTIES
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

# F6: a RIB-supplied parameter array shorter than its declared class used to
# read past its own allocation instead of clamping. See
# tests/paramclamp_test.cpp.
add_executable(gman_paramclamp paramclamp_test.cpp)
target_link_libraries(gman_paramclamp PRIVATE gman_core)

add_test(NAME paramclamp
  COMMAND gman_paramclamp $<TARGET_FILE:gman>
                          "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed")
set_tests_properties(paramclamp PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# A RIB integer narrows twice, unchecked, on its way to RtInt: the
# tokenizer's strtol, then nextInt/toRtIntVector's own implicit
# conversion. See tests/ribintegerbounds_test.cpp.
add_executable(gman_ribintegerbounds ribintegerbounds_test.cpp)
target_link_libraries(gman_ribintegerbounds PRIVATE gman_core)

add_test(NAME ribintegerbounds
  COMMAND gman_ribintegerbounds $<TARGET_FILE:gman>
                                 "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed")
set_tests_properties(ribintegerbounds PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# Polygon and the seven quadrics built their GMANParameterList with no
# suppliedCounts at all, so a short array read past its own allocation
# instead of clamping. See tests/geometryparamclamp_test.cpp.
add_executable(gman_geometryparamclamp geometryparamclamp_test.cpp)
target_link_libraries(gman_geometryparamclamp PRIVATE gman_core)

add_test(NAME geometryparamclamp
  COMMAND gman_geometryparamclamp $<TARGET_FILE:gman>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed")
set_tests_properties(geometryparamclamp PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# A declared-integer parameter used to be stored as RtFloat and read back
# through an (RtInt*) cast -- float bits reinterpreted as an integer, not
# precision loss. See tests/paramlistdeclaredtype_test.cpp; its own final
# check drives GMANRIBParse in process (RiBegin dlopens the default
# renderer by bare name, hence the same LD_LIBRARY_PATH/DYLD_LIBRARY_PATH
# as tests/basicstate.c).
add_executable(gman_paramlistdeclaredtype paramlistdeclaredtype_test.cpp)
target_link_libraries(gman_paramlistdeclaredtype PRIVATE gman_core)

add_test(NAME paramlistdeclaredtype
  COMMAND gman_paramlistdeclaredtype $<TARGET_FILE:gman>
                                      "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed")
set_tests_properties(paramlistdeclaredtype PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# GMANRIBTokenize::parseString gained escape decoding so an embedded \"
# stops desyncing the parse. See tests/ribstringescape_test.cpp; its own
# in-process check drives GMANRIBParse directly, hence the same
# LD_LIBRARY_PATH/DYLD_LIBRARY_PATH as tests/basicstate.c.
add_executable(gman_ribstringescape ribstringescape_test.cpp)
target_link_libraries(gman_ribstringescape PRIVATE gman_core)

add_test(NAME ribstringescape
  COMMAND gman_ribstringescape $<TARGET_FILE:gman>
                               "${CMAKE_CURRENT_SOURCE_DIR}/rib")
set_tests_properties(ribstringescape PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# Bilinear and bicubic Patch rasterization. See tests/patchnorender_test.cpp.
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

# Bilinear and bicubic PatchMesh rasterization. See tests/patchmesh_test.cpp:
# hand-computed control-grid points (bilinear and bicubic, periodic and
# nonperiodic, mixed wrap) checked against GMANPatchMesh directly, plus
# every tests/rib/patchmesh_*.rib fixture rendered and read back.
add_executable(gman_patchmesh patchmesh_test.cpp)
target_link_libraries(gman_patchmesh PRIVATE gman_core TIFF::TIFF)

set(GMAN_PATCHMESH_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/patchmesh-run")
file(MAKE_DIRECTORY "${GMAN_PATCHMESH_WORKDIR}")

add_test(NAME patchmesh
  COMMAND gman_patchmesh $<TARGET_FILE:gman>
                         "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_PATCHMESH_WORKDIR}")
set_tests_properties(patchmesh PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

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
# wrote column 0. The binary needs libpng to link, so it stays guarded; the
# test itself registers unconditionally so a build without libpng reports
# the gap as Skipped rather than making the PNG driver invisible to ctest.
if(GMAN_WITH_PNG)
  add_executable(gman_pngdriver pngdriver_test.cpp)
  target_link_libraries(gman_pngdriver PRIVATE gman_core PNG::PNG TIFF::TIFF)

  set(GMAN_PNGDRIVER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/pngdriver-run")
  file(MAKE_DIRECTORY "${GMAN_PNGDRIVER_WORKDIR}")

  set(GMAN_PNGDRIVER_COMMAND gman_pngdriver $<TARGET_FILE:gman>)
else()
  set(GMAN_PNGDRIVER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}")
  set(GMAN_PNGDRIVER_COMMAND sh -c "exit 77")
endif()

add_test(NAME pngdriver
  COMMAND ${GMAN_PNGDRIVER_COMMAND}
  WORKING_DIRECTORY "${GMAN_PNGDRIVER_WORKDIR}")
set_tests_properties(pngdriver PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30
  SKIP_RETURN_CODE 77)

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

# The driver table's own contract: gman --version lists exactly the
# drivers this configure actually compiled, in table order.
set(GMAN_EXPECTED_DRIVERS "tiff pnm")
if(GMAN_WITH_PNG)
  string(APPEND GMAN_EXPECTED_DRIVERS " png")
endif()
if(GMAN_WITH_JPEG)
  string(APPEND GMAN_EXPECTED_DRIVERS " jpeg")
endif()

add_executable(gman_filedrivers filedrivers_test.cpp)
target_link_libraries(gman_filedrivers PRIVATE gman_core)

set(GMAN_FILEDRIVERS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/filedrivers-run")
file(MAKE_DIRECTORY "${GMAN_FILEDRIVERS_WORKDIR}")

add_test(NAME filedrivers
  COMMAND gman_filedrivers $<TARGET_FILE:gman> "${GMAN_EXPECTED_DRIVERS}"
  WORKING_DIRECTORY "${GMAN_FILEDRIVERS_WORKDIR}")
set_tests_properties(filedrivers PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Pins gman's one-line startup banner against a version-bump regression.
add_executable(gman_banner banner_test.cpp)
target_link_libraries(gman_banner PRIVATE gman_core)

set(GMAN_BANNER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/banner-run")
file(MAKE_DIRECTORY "${GMAN_BANNER_WORKDIR}")

add_test(NAME banner
  COMMAND gman_banner $<TARGET_FILE:gman>
                       "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere.rib"
                       "${PROJECT_VERSION}"
  WORKING_DIRECTORY "${GMAN_BANNER_WORKDIR}")
set_tests_properties(banner PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# A known but disabled driver's extension must fail loudly (RIE_BADFILE,
# naming the missing library), not silently. Registered unconditionally
# and always compiled -- it reads which drivers exist from gman --version
# at runtime instead of a compile-time #ifdef, so it runs unchanged across
# the ON, OFF and mixed configurations. With both drivers enabled there is
# nothing disabled to exercise, so it self-skips (exit 77); drivers-off is
# where it runs for real.
add_executable(gman_displaydisabled displaydisabled_test.cpp)
target_link_libraries(gman_displaydisabled PRIVATE gman_core)

set(GMAN_DISPLAYDISABLED_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/displaydisabled-run")
file(MAKE_DIRECTORY "${GMAN_DISPLAYDISABLED_WORKDIR}")

add_test(NAME displaydisabled
  COMMAND gman_displaydisabled $<TARGET_FILE:gman>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_DISPLAYDISABLED_WORKDIR}")
set_tests_properties(displaydisabled PROPERTIES
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
                       "${CMAKE_CURRENT_SOURCE_DIR}/rib"
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

# gman::sampleCentre: the raster centre of one sample, and the deleted
# (T, U, V) overload that keeps a non-int argument from truncating
# silently, pinned at compile time via a requires-expression.
add_executable(gman_samplecentre samplecentre_test.cpp)
target_link_libraries(gman_samplecentre PRIVATE gman_core)

add_test(NAME samplecentre COMMAND gman_samplecentre)
set_tests_properties(samplecentre PROPERTIES
  LABELS unit
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

# The texture cache (gmantexture.h), the texture() shadeop and
# gman::makeTexture: direct sampler and writer assertions, tests/rib/texture.rib
# (a paintedplastic Patch read back for its four quadrants' colours) and
# tests/rib/maketexture_wrap.rib (the same, through a MakeTexture request).
add_executable(gman_texture texture_test.cpp)
target_link_libraries(gman_texture PRIVATE gman_core TIFF::TIFF gman_internal_headers)

set(GMAN_TEXTURE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/texture-run")
file(MAKE_DIRECTORY "${GMAN_TEXTURE_WORKDIR}")

add_test(NAME texture
  COMMAND gman_texture $<TARGET_FILE:gman>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_TEXTURE_WORKDIR}")
set_tests_properties(texture PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# s, t (and, for a polygon, u, v) reaching texture() on quadrics, Patch and
# (General)Polygon: RiTextureCoordinates and the "s"/"t"/"st" primvars, over
# tests/rib/texcoords/*.rib -- each fixture copies tests/rib/texture.rib's
# own frame and checker.
add_executable(gman_texcoords texcoords_test.cpp)
target_link_libraries(gman_texcoords PRIVATE gman_core TIFF::TIFF)

set(GMAN_TEXCOORDS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/texcoords-run")
file(MAKE_DIRECTORY "${GMAN_TEXCOORDS_WORKDIR}")

add_test(NAME texcoords
  COMMAND gman_texcoords $<TARGET_FILE:gman>
                         "${CMAKE_CURRENT_SOURCE_DIR}/rib/texcoords"
  WORKING_DIRECTORY "${GMAN_TEXCOORDS_WORKDIR}")
set_tests_properties(texcoords PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

# PointsPolygons and PointsGeneralPolygons: every face is the equivalent
# Polygon/GeneralPolygon, gathered through "verts" out of a shared "P".
# Direct object-manager calls, twin-RIB renders compared pixel by pixel
# (tests/goldenimage.h) and malformed request fixtures. See
# tests/pointspolygons_test.cpp.
add_executable(gman_pointspolygons pointspolygons_test.cpp)
target_link_libraries(gman_pointspolygons PRIVATE gman_core TIFF::TIFF)

set(GMAN_POINTSPOLYGONS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/pointspolygons-run")
file(MAKE_DIRECTORY "${GMAN_POINTSPOLYGONS_WORKDIR}")

add_test(NAME pointspolygons
  COMMAND gman_pointspolygons $<TARGET_FILE:gman>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_POINTSPOLYGONS_WORKDIR}")
set_tests_properties(pointspolygons PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

# NuPatch: GMANNuPatch now implements GMANParametric with a Cox-de Boor
# evaluator, RiNuPatchV validates and sizes the request, parseNuPatch
# checks knot lengths and owns its arrays. Evaluator cases (direct
# construction), twin-RIB renders and texture-coordinates no-op fixtures
# compared pixel by pixel (tests/goldenimage.h), and malformed/exception
# fixtures. See tests/nupatch_test.cpp.
add_executable(gman_nupatch nupatch_test.cpp)
target_link_libraries(gman_nupatch PRIVATE gman_core TIFF::TIFF)

set(GMAN_NUPATCH_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/nupatch-run")
file(MAKE_DIRECTORY "${GMAN_NUPATCH_WORKDIR}")

add_test(NAME nupatch
  COMMAND gman_nupatch $<TARGET_FILE:gman>
                       "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_NUPATCH_WORKDIR}")
set_tests_properties(nupatch PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

# Test-only surface shader (see its own comment): a dlopen'd module and its
# caller share nothing but computeCi's own return value, so
# environment_test.cpp's plumbing proof needs a real, loadable plugin to
# read GMANSurfaceEnv::cameraToWorld back out of a real shading call.
# Never dlopened outside this test; not part of the shipped renderer.
gman_add_plugin(gman_camerashader camerashader_test_plugin.cpp)
set_target_properties(gman_camerashader PROPERTIES OUTPUT_NAME "camerashader")

# environment(), MakeLatLongEnvironment and shinymetal (RISpec 3.2 Sec
# 7.1.2, Sec 15.7.2, Appendix A.2.4): camera-to-world reaching the shading
# path, the writer and lookup, and shinymetal rendered under three camera
# orientations. See tests/environment_test.cpp.
add_executable(gman_environment environment_test.cpp)
target_link_libraries(gman_environment PRIVATE gman_core TIFF::TIFF gman_internal_headers)
add_dependencies(gman_environment gman_camerashader gman_shinymetal)

set(GMAN_ENVIRONMENT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/environment-run")
file(MAKE_DIRECTORY "${GMAN_ENVIRONMENT_WORKDIR}")

add_test(NAME environment
  COMMAND gman_environment $<TARGET_FILE:gman>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_ENVIRONMENT_WORKDIR}")
set_tests_properties(environment PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

# gmanlog's sink: setLogFile used to hold logMutex across a call to info(),
# which deadlocked. Also proves one trailing newline, a suppressed level
# never formatting, and eight concurrent writers landing 8000 whole lines.
add_executable(gman_log log_test.cpp)
target_link_libraries(gman_log PRIVATE gman_core)

set(GMAN_LOG_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/log-run")
file(MAKE_DIRECTORY "${GMAN_LOG_WORKDIR}")

add_test(NAME log
  COMMAND gman_log
  WORKING_DIRECTORY "${GMAN_LOG_WORKDIR}")
set_tests_properties(log PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::parallelFor's contract: exactly once per index, the serial path at
# workers=1, worker indices and thread ownership, actual parallelism, and
# an exception from one body stopping every worker before reaching the
# calling thread.
add_executable(gman_parallel parallel_test.cpp)
target_link_libraries(gman_parallel PRIVATE gman_core gman_internal_headers)

add_test(NAME parallel COMMAND gman_parallel)
set_tests_properties(parallel PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Threading lives behind one seam: no header, and no source file other
# than gmanparallel.cpp/gmanlog.cpp, may include a threading header or
# name a thread primitive.
add_executable(gman_threadcontainment threadcontainment_test.cpp)
add_test(NAME threadcontainment
  COMMAND gman_threadcontainment
          "${CMAKE_SOURCE_DIR}/include"
          "${CMAKE_SOURCE_DIR}/libgman"
          "${CMAKE_SOURCE_DIR}/libgmanrib"
          "${CMAKE_SOURCE_DIR}/shaders"
          "${CMAKE_SOURCE_DIR}/renderers"
          "${CMAKE_SOURCE_DIR}/gman"
          "${CMAKE_SOURCE_DIR}/gmansl")
set_tests_properties(threadcontainment PROPERTIES
  LABELS unit
  TIMEOUT 30)

# tolower/toupper are undefined for a byte above 127 unless the argument is
# cast through unsigned char first. See tests/tolowerscan_test.cpp.
add_executable(gman_tolowerscan tolowerscan_test.cpp)
add_test(NAME tolowerscan
  COMMAND gman_tolowerscan "${CMAKE_SOURCE_DIR}/libgman")
set_tests_properties(tolowerscan PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::gammaCorrected: identity at gain 1/gamma 1 (no golden image moves), the
# pow(gain * channel, 1/gamma) shape, and the sub-byte precision case the
# deleted byte lookup table could never produce.
add_executable(gman_gamma gamma_test.cpp)
target_link_libraries(gman_gamma PRIVATE gman_core)

add_test(NAME gamma COMMAND gman_gamma)
set_tests_properties(gamma PROPERTIES
  LABELS unit
  TIMEOUT 30)

# R7 proof, check 1: GMANSurfaceEnv's occlusion hook -- diffuse()/specular()
# scale each non-ambient light by occluder->transmission(), ambient() never
# consults it, and a point/distant light reports the distance and direction
# the contract promises.
add_executable(gman_occlusion occlusion_test.cpp)
target_link_libraries(gman_occlusion PRIVATE gman_core)

add_test(NAME occlusion COMMAND gman_occlusion)
set_tests_properties(occlusion PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Ray tracer core, unit 1: GMANViewingSystem answers a camera-space ray
# (cameraRay) as well as a world-space one (ray), and pins the [tmin,
# tmax] interval every GMANRay defaults to.
add_executable(gman_cameraray cameraray_test.cpp)
target_link_libraries(gman_cameraray PRIVATE gman_core gman_internal_headers)

add_test(NAME cameraray COMMAND gman_cameraray)
set_tests_properties(cameraray PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Ray tracer core, unit 2: GMANQuadraticRoots solves a*t^2 + b*t + c == 0
# with the numerically stable root form, pinning its root count and
# ordering at every discriminant sign.
add_executable(gman_quadratic quadratic_test.cpp)
target_link_libraries(gman_quadratic PRIVATE gman_core)

add_test(NAME quadratic COMMAND gman_quadratic)
set_tests_properties(quadratic PROPERTIES
  LABELS unit
  TIMEOUT 30)

# R5b review proof: solveRayQuadratic, the linear-case helper the four
# swept quadrics' intersectors share, at coefficients a caller's own
# floating-point contraction cannot perturb.
add_executable(gman_rayquadratic rayquadratic_test.cpp)
target_link_libraries(gman_rayquadratic PRIVATE gman_core)
target_include_directories(gman_rayquadratic PRIVATE "${CMAKE_SOURCE_DIR}/renderers/raytracer")

add_test(NAME rayquadratic COMMAND gman_rayquadratic)
set_tests_properties(rayquadratic PROPERTIES
  LABELS unit
  TIMEOUT 30)

# R5c proof, part A: gman::solveQuartic against quartics built from known
# dyadic roots, linked against the shared object library (it carries the
# compiled solver, unlike solveRayQuadratic's header-only one) for the same
# reason as gman_raysphere below.
add_executable(gman_rayquartic rayquartic_test.cpp)
target_link_libraries(gman_rayquartic PRIVATE gman_core gman_raytracer_objects)

add_test(NAME rayquartic COMMAND gman_rayquartic)
set_tests_properties(rayquartic PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Ray tracer core, unit 2: GMANRaySphere::intersect, linked against the
# shared raytracer object library rather than the gman_raytracer plugin
# target, so this still builds and runs with GMAN_BUILD_RAYTRACER off.
add_executable(gman_raysphere raysphere_test.cpp)
target_link_libraries(gman_raysphere PRIVATE gman_core gman_raytracer_objects)

add_test(NAME raysphere COMMAND gman_raysphere)
set_tests_properties(raysphere PROPERTIES
  LABELS unit
  TIMEOUT 30)

# R5a proof, unit: GMANRayDisk::intersect, linked against the shared object
# library for the same reason as gman_raysphere above.
add_executable(gman_raydisk raydisk_test.cpp)
target_link_libraries(gman_raydisk PRIVATE gman_core gman_raytracer_objects)

add_test(NAME raydisk COMMAND gman_raydisk)
set_tests_properties(raydisk PROPERTIES
  LABELS unit
  TIMEOUT 30)

# R5a proof, unit: GMANRayPolygon::intersect, linked against the shared
# object library for the same reason as gman_raysphere above.
add_executable(gman_raypolygon raypolygon_test.cpp)
target_link_libraries(gman_raypolygon PRIVATE gman_core gman_raytracer_objects)

add_test(NAME raypolygon COMMAND gman_raypolygon)
set_tests_properties(raypolygon PROPERTIES
  LABELS unit
  TIMEOUT 30)

# R5b proof, unit: GMANRayCylinder::intersect, linked against the shared
# object library for the same reason as gman_raysphere above.
add_executable(gman_raycylinder raycylinder_test.cpp)
target_link_libraries(gman_raycylinder PRIVATE gman_core gman_raytracer_objects)

add_test(NAME raycylinder COMMAND gman_raycylinder)
set_tests_properties(raycylinder PROPERTIES
  LABELS unit
  TIMEOUT 30)

# R5b proof, unit: GMANRayCone::intersect, linked against the shared object
# library for the same reason as gman_raysphere above.
add_executable(gman_raycone raycone_test.cpp)
target_link_libraries(gman_raycone PRIVATE gman_core gman_raytracer_objects)

add_test(NAME raycone COMMAND gman_raycone)
set_tests_properties(raycone PROPERTIES
  LABELS unit
  TIMEOUT 30)

# R5b proof, unit: GMANRayParaboloid::intersect, linked against the shared
# object library for the same reason as gman_raysphere above.
add_executable(gman_rayparaboloid rayparaboloid_test.cpp)
target_link_libraries(gman_rayparaboloid PRIVATE gman_core gman_raytracer_objects)

add_test(NAME rayparaboloid COMMAND gman_rayparaboloid)
set_tests_properties(rayparaboloid PROPERTIES
  LABELS unit
  TIMEOUT 30)

# R5b proof, unit: GMANRayHyperboloid::intersect, linked against the shared
# object library for the same reason as gman_raysphere above.
add_executable(gman_rayhyperboloid rayhyperboloid_test.cpp)
target_link_libraries(gman_rayhyperboloid PRIVATE gman_core gman_raytracer_objects)

add_test(NAME rayhyperboloid COMMAND gman_rayhyperboloid)
set_tests_properties(rayhyperboloid PROPERTIES
  LABELS unit
  TIMEOUT 30)

# The flat-annulus hyperboloid (point1.z == point2.z): z carries no
# information about v on this plane, so v comes from the plane hit's own
# radius, and the wedge is a spiral sector since phi(v) varies with v.
add_executable(gman_hyperboloidflatannulus hyperboloidflatannulus_test.cpp)
target_link_libraries(gman_hyperboloidflatannulus PRIVATE gman_core gman_raytracer_objects)

add_test(NAME hyperboloidflatannulus COMMAND gman_hyperboloidflatannulus)
set_tests_properties(hyperboloidflatannulus PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Ray tracer core, unit 3: GMANRayObjectManager::getRSSphere builds a
# GMANRaySphere holding its parameters and a copied transform, linked
# against the shared raytracer object library for the same reason as
# gman_raysphere above.
add_executable(gman_rayobjectmanager rayobjectmanager_test.cpp)
target_link_libraries(gman_rayobjectmanager PRIVATE gman_core gman_raytracer_objects)

add_test(NAME rayobjectmanager COMMAND gman_rayobjectmanager)
set_tests_properties(rayobjectmanager PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# R4 proof, unit 4: GMANRaytraceRenderer::getDepth, linked against the
# shared object library for the same reason as gman_raysphere above. A
# default GMANAttributes loads libmatte.so by bare name, so this carries the
# same ENVIRONMENT as gman_rayobjectmanager.
add_executable(gman_raydepth raydepth_test.cpp)
target_link_libraries(gman_raydepth PRIVATE gman_core gman_internal_headers gman_raytracer_objects)

add_test(NAME raydepth COMMAND gman_raydepth)
set_tests_properties(raydepth PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# R7 proof, check 5: GMANRayOccluder::transmission, linked against the
# shared object library for the same reason as gman_raysphere above;
# carries raydepth's own ENVIRONMENT for the same reason too.
add_executable(gman_rayoccluder rayoccluder_test.cpp)
target_link_libraries(gman_rayoccluder PRIVATE gman_core gman_internal_headers gman_raytracer_objects)

add_test(NAME rayoccluder COMMAND gman_rayoccluder)
set_tests_properties(rayoccluder PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# R-transparency proof, check 5: the composite loop's own layer cap, linked
# against the shared object library for the same reason as gman_raysphere
# above; carries raydepth's own ENVIRONMENT for the same reason too.
add_executable(gman_compositelayercap compositelayercap_test.cpp)
target_link_libraries(gman_compositelayercap PRIVATE gman_core gman_internal_headers gman_raytracer_objects)

add_test(NAME compositelayercap COMMAND gman_compositelayercap)
set_tests_properties(compositelayercap PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# R7 proof, §8 check 7: GMANRayOccluder::transmission's own iteration cap,
# linked against the shared object library for the same reason as
# gman_raysphere above; carries raydepth's own ENVIRONMENT for the same
# reason too. See coincidenttransmission_test.cpp's own comment.
add_executable(gman_coincidenttransmission coincidenttransmission_test.cpp)
target_link_libraries(gman_coincidenttransmission PRIVATE gman_core gman_internal_headers gman_raytracer_objects)

add_test(NAME coincidenttransmission COMMAND gman_coincidenttransmission)
set_tests_properties(coincidenttransmission PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# R7 proof, §8 check 6: two close, camera-facing transparent disks, linked
# against the shared object library for the same reason as gman_raysphere
# above; carries raydepth's own ENVIRONMENT for the same reason too. See
# closetransparentstack_test.cpp's own comment.
add_executable(gman_closetransparentstack closetransparentstack_test.cpp)
target_link_libraries(gman_closetransparentstack PRIVATE gman_core gman_internal_headers gman_raytracer_objects)

add_test(NAME closetransparentstack COMMAND gman_closetransparentstack)
set_tests_properties(closetransparentstack PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# R7 proof, §8 check 8: a grazing transparent hit, linked against the
# shared object library for the same reason as gman_raysphere above;
# carries raydepth's own ENVIRONMENT for the same reason too. See
# grazingtransparent_test.cpp's own comment.
add_executable(gman_grazingtransparent grazingtransparent_test.cpp)
target_link_libraries(gman_grazingtransparent PRIVATE gman_core gman_internal_headers gman_raytracer_objects)

add_test(NAME grazingtransparent COMMAND gman_grazingtransparent)
set_tests_properties(grazingtransparent PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# `gman -r gmanraytracer` renders tests/rib/r4_raytracer.rib to a round,
# plastic-shaded sphere matching a checked-in golden image and differing
# from the z-buffer's own render of the same scene. Registered only when
# the plugin builds, since `-r gmanraytracer` has nothing to load
# otherwise.
if(GMAN_BUILD_RAYTRACER)
  add_executable(gman_rayrender rayrender_test.cpp)
  target_link_libraries(gman_rayrender PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_rayrender gman_raytracer)

  set(GMAN_RAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/rayrender-run")
  file(MAKE_DIRECTORY "${GMAN_RAYRENDER_WORKDIR}")

  add_test(NAME rayrender
    COMMAND gman_rayrender $<TARGET_FILE:gman>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_RAYRENDER_WORKDIR}")
  set_tests_properties(rayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # R5a proof: `gman -r gmanraytracer` renders tests/rib/r5a_disk.rib, a
  # partial-thetamax disk, matching a checked-in golden image. See
  # tests/diskrayrender_test.cpp.
  add_executable(gman_diskrayrender diskrayrender_test.cpp)
  target_link_libraries(gman_diskrayrender PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_diskrayrender gman_raytracer)

  set(GMAN_DISKRAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/diskrayrender-run")
  file(MAKE_DIRECTORY "${GMAN_DISKRAYRENDER_WORKDIR}")

  add_test(NAME diskrayrender
    COMMAND gman_diskrayrender $<TARGET_FILE:gman>
                               "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_DISKRAYRENDER_WORKDIR}")
  set_tests_properties(diskrayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # R5b proof: `gman -r gmanraytracer` renders tests/rib/r5b_quadrics.rib,
  # a cylinder, cone, paraboloid and hyperboloid side by side, matching a
  # checked-in golden image. See tests/quadricsrayrender_test.cpp.
  add_executable(gman_quadricsrayrender quadricsrayrender_test.cpp)
  target_link_libraries(gman_quadricsrayrender PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_quadricsrayrender gman_raytracer)

  set(GMAN_QUADRICSRAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/quadricsrayrender-run")
  file(MAKE_DIRECTORY "${GMAN_QUADRICSRAYRENDER_WORKDIR}")

  add_test(NAME quadricsrayrender
    COMMAND gman_quadricsrayrender $<TARGET_FILE:gman>
                                   "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_QUADRICSRAYRENDER_WORKDIR}")
  set_tests_properties(quadricsrayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # R5a proof: `gman -r gmanraytracer` renders tests/rib/r5a_polygon.rib, a
  # camera-facing pentagon, matching a checked-in golden image. See
  # tests/polygonrayrender_test.cpp.
  add_executable(gman_polygonrayrender polygonrayrender_test.cpp)
  target_link_libraries(gman_polygonrayrender PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_polygonrayrender gman_raytracer)

  set(GMAN_POLYGONRAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/polygonrayrender-run")
  file(MAKE_DIRECTORY "${GMAN_POLYGONRAYRENDER_WORKDIR}")

  add_test(NAME polygonrayrender
    COMMAND gman_polygonrayrender $<TARGET_FILE:gman>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_POLYGONRAYRENDER_WORKDIR}")
  set_tests_properties(polygonrayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # R5a proof: the same tests/rib/r5a_polygon.rib fixture, rendered by both
  # renderers and compared away from the silhouette edge, where the two
  # renderers' differing sample placement is expected to disagree. See
  # tests/polygonzbuffer_test.cpp.
  add_executable(gman_polygonzbuffer polygonzbuffer_test.cpp)
  target_link_libraries(gman_polygonzbuffer PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_polygonzbuffer gman_raytracer)

  set(GMAN_POLYGONZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/polygonzbuffer-run")
  file(MAKE_DIRECTORY "${GMAN_POLYGONZBUFFER_WORKDIR}")

  add_test(NAME polygonzbuffer
    COMMAND gman_polygonzbuffer $<TARGET_FILE:gman>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_POLYGONZBUFFER_WORKDIR}")
  set_tests_properties(polygonzbuffer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # R7 proof, checks 3-4: tests/rib/shadow.rib -- a small sphere between a
  # distant light and a large receiver -- casts a shadow a checked-in
  # golden pins, and one pixel inside it holds two further relations
  # against shadow_nolight.rib and shadow_noblocker.rib. See
  # shadowrender_test.cpp's own comment.
  add_executable(gman_shadowrender shadowrender_test.cpp)
  target_link_libraries(gman_shadowrender PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_shadowrender gman_raytracer)

  set(GMAN_SHADOWRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/shadowrender-run")
  file(MAKE_DIRECTORY "${GMAN_SHADOWRENDER_WORKDIR}")

  add_test(NAME shadowrender
    COMMAND gman_shadowrender $<TARGET_FILE:gman>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_SHADOWRENDER_WORKDIR}")
  set_tests_properties(shadowrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # R-transparency proof, checks 2-3: tests/rib/transparency_composite.rib
  # -- a semi-transparent square over a larger opaque one -- matches a
  # checked-in golden, and its centre pixel holds two further relations
  # against transparency_front_alone.rib, transparency_front_opaque.rib
  # and transparency_back_alone.rib. See transparencyrender_test.cpp's own
  # comment.
  add_executable(gman_transparencyrender transparencyrender_test.cpp)
  target_link_libraries(gman_transparencyrender PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_transparencyrender gman_raytracer)

  set(GMAN_TRANSPARENCYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/transparencyrender-run")
  file(MAKE_DIRECTORY "${GMAN_TRANSPARENCYRENDER_WORKDIR}")

  add_test(NAME transparencyrender
    COMMAND gman_transparencyrender $<TARGET_FILE:gman>
                                    "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_TRANSPARENCYRENDER_WORKDIR}")
  set_tests_properties(transparencyrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # R-transparency proof, check 4: tests/rib/transparencyshadow_none.rib,
  # transparencyshadow_one.rib, transparencyshadow_two.rib and
  # transparencyshadow_opaque.rib share one receiver and one light; the
  # shadowed pixel reads strictly none > one > two > opaque, each gap
  # wider than GOLDEN_CHANNEL_TOL. See
  # transparencyshadowrender_test.cpp's own comment.
  add_executable(gman_transparencyshadowrender transparencyshadowrender_test.cpp)
  target_link_libraries(gman_transparencyshadowrender PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_transparencyshadowrender gman_raytracer)

  set(GMAN_TRANSPARENCYSHADOWRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/transparencyshadowrender-run")
  file(MAKE_DIRECTORY "${GMAN_TRANSPARENCYSHADOWRENDER_WORKDIR}")

  add_test(NAME transparencyshadowrender
    COMMAND gman_transparencyshadowrender $<TARGET_FILE:gman>
                                          "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_TRANSPARENCYSHADOWRENDER_WORKDIR}")
  set_tests_properties(transparencyshadowrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # R7 proof, §8 check 1: tests/rib/contactshadow.rib -- a thin vertical
  # blocker resting on a horizontal floor at gap 0.001 -- against
  # contactshadow_noblocker.rib, pinning that the receiver strip beside the
  # blocker's base darkens once the shadow ray's origin offset lands. See
  # contactshadow_test.cpp's own comment.
  add_executable(gman_contactshadow contactshadow_test.cpp)
  target_link_libraries(gman_contactshadow PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_contactshadow gman_raytracer)

  set(GMAN_CONTACTSHADOW_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/contactshadow-run")
  file(MAKE_DIRECTORY "${GMAN_CONTACTSHADOW_WORKDIR}")

  add_test(NAME contactshadow
    COMMAND gman_contactshadow $<TARGET_FILE:gman>
                               "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_CONTACTSHADOW_WORKDIR}")
  set_tests_properties(contactshadow PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # R7 proof, §8 check 2: tests/rib/acnesphere_x1.rib, acnesphere_x1000.rib
  # and acnesphere_xsmall.rib -- one lit sphere at three scales -- pinning
  # that self-shadow acne is scale-independent and that no pixel inside the
  # silhouette dips below both its horizontal neighbors. See
  # acnesphere_test.cpp's own comment.
  add_executable(gman_acnesphere acnesphere_test.cpp)
  target_link_libraries(gman_acnesphere PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_acnesphere gman_raytracer)

  set(GMAN_ACNESPHERE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/acnesphere-run")
  file(MAKE_DIRECTORY "${GMAN_ACNESPHERE_WORKDIR}")

  add_test(NAME acnesphere
    COMMAND gman_acnesphere $<TARGET_FILE:gman>
                            "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_ACNESPHERE_WORKDIR}")
  set_tests_properties(acnesphere PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # Fix proof: tests/rib/surfaceparameters_two_surfaces.rib,
  # surfaceparameters_restored.rib and surfaceparameters_default.rib pin
  # that every surface shades with its own RiSurface's parameter list, one
  # fixture per §1 form of the defect. See surfaceparameters_test.cpp's
  # own comment.
  add_executable(gman_surfaceparameters surfaceparameters_test.cpp)
  target_link_libraries(gman_surfaceparameters PRIVATE gman_core TIFF::TIFF)
  add_dependencies(gman_surfaceparameters gman_raytracer)

  set(GMAN_SURFACEPARAMETERS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/surfaceparameters-run")
  file(MAKE_DIRECTORY "${GMAN_SURFACEPARAMETERS_WORKDIR}")

  add_test(NAME surfaceparameters
    COMMAND gman_surfaceparameters $<TARGET_FILE:gman>
                                   "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_SURFACEPARAMETERS_WORKDIR}")
  set_tests_properties(surfaceparameters PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# Proves both halves of an installed gman. bin/gman renders every
# shipped shader (matte, plastic, metal, paintedplastic, shinymetal) from a
# fresh cmake --install prefix, and a second, unrelated CMake project finds
# it with find_package(gman), links gman::gman_core, compiles <gman/ri.h>
# and runs. See tests/install_test.cmake for the three steps and
# tests/install/ for the consumer project it builds.
#
# cmake --install (step 1) reads libgmanrib.so from the build tree, so this
# carries the same RESOURCE_LOCK as the ribwriter tests above.
#
# No ENVIRONMENT property: see tests/install_test.cmake's step 2 for why.
add_test(NAME install
  COMMAND "${CMAKE_COMMAND}"
          -D "GMAN_BUILD_DIR=${CMAKE_BINARY_DIR}"
          -D "GMAN_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
          -D "GMAN_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/install-run/prefix"
          -D "GMAN_RENDER_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-run/render"
          -D "GMAN_CONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/install"
          -D "GMAN_CONSUMER_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-run/consumer-build"
          -D "GMAN_BINDIR=${CMAKE_INSTALL_BINDIR}"
          -D "GMAN_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
          -D "GMAN_VERSION=${PROJECT_VERSION}"
          -D "GMAN_GENERATOR=${CMAKE_GENERATOR}"
          -D "GMAN_C_COMPILER=${CMAKE_C_COMPILER}"
          -D "GMAN_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
          -D "GMAN_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
          -D "GMAN_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
          -D "GMAN_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
          -D "GMAN_HAS_RAYTRACER=${GMAN_BUILD_RAYTRACER}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/install_test.cmake")
set_tests_properties(install PROPERTIES
  LABELS render
  TIMEOUT 300
  RESOURCE_LOCK gmanrib_plugin_file)
