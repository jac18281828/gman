# SPDX-License-Identifier: LGPL-2.1-or-later

# A test that includes a libgman/ header links this instead of adding
# libgman/ to its own include path.
add_library(gman_internal_headers INTERFACE)
target_include_directories(gman_internal_headers INTERFACE
  "${CMAKE_SOURCE_DIR}/libgman")

# The same for a test that includes a private libgmanray/ header.
add_library(gman_ray_internal_headers INTERFACE)
target_include_directories(gman_ray_internal_headers INTERFACE
  "${CMAKE_SOURCE_DIR}/libgmanray")

# GMAN_SOURCE_DIRS as absolute paths, the arguments every source-scanning test
# takes.
list(TRANSFORM GMAN_SOURCE_DIRS PREPEND "${CMAKE_SOURCE_DIR}/"
  OUTPUT_VARIABLE GMAN_SOURCE_PATHS)

# The runtime baseline: exit status, message and image output for a minimal
# scene. See tests/baseline_test.cpp for the full invariant.
add_executable(gman_baseline baseline_test.cpp)
target_link_libraries(gman_baseline PRIVATE gman TIFF::TIFF)

# Run in a scratch directory so the image the RIB names would land there, and
# so the assertion that none appears means something.
set(GMAN_BASELINE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/baseline-run")
file(MAKE_DIRECTORY "${GMAN_BASELINE_WORKDIR}")

add_test(NAME baseline
  COMMAND gman_baseline $<TARGET_FILE:gman_cli>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere_ambient.rib"
  WORKING_DIRECTORY "${GMAN_BASELINE_WORKDIR}")

# gman dlopens lib<renderer>.so by bare name out of the build tree.
set_tests_properties(baseline PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# The RI state machine, through the C API. Replaces the shipped
# tests/basicstate.c, which could not work as written.
add_executable(gman_basicstate basicstate.c)
target_link_libraries(gman_basicstate PRIVATE gman)

# A C program against a C++ library has to be linked by the C++ driver. With
# the plain C driver clang omits the C++ runtimes, which is invisible in a
# normal build (libgman's DT_NEEDED covers libstdc++) but fatal under the
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
target_link_libraries(gman_ribwriter PRIVATE gman)
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

# Drives RiSurface, RiOption, RiPolygon, RiSphere and RiMakeShadow through
# their ellipsis form (not *V), so each reads its own va_list through the
# same RIB-writer seam as ribwriter above. See tests/capivalist.c.
add_executable(gman_capivalist capivalist.c)
target_link_libraries(gman_capivalist PRIVATE gman)
set_target_properties(gman_capivalist PROPERTIES LINKER_LANGUAGE CXX)
add_dependencies(gman_capivalist gman_rib)

set(GMAN_CAPIVALIST_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/capivalist-run")
file(MAKE_DIRECTORY "${GMAN_CAPIVALIST_WORKDIR}")

add_test(NAME capivalist
  COMMAND gman_capivalist
          "${GMAN_CAPIVALIST_WORKDIR}/scene.rib"
          "${CMAKE_CURRENT_SOURCE_DIR}/rib/golden/capivalist_scene.rib"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(capivalist PROPERTIES
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
target_link_libraries(gman_ribwriterfailure PRIVATE gman)
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
# exit: the three points where a dlopen'd plugin module, which stays loaded, is
# reached again. See tests/shaderoutlivesrenderer_test.c.
add_executable(gman_shaderoutlivesrenderer shaderoutlivesrenderer_test.c)
target_link_libraries(gman_shaderoutlivesrenderer PRIVATE gman)
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
target_link_libraries(gman_paramlist PRIVATE gman)

add_test(NAME paramlist COMMAND gman_paramlist)
set_tests_properties(paramlist PROPERTIES
  LABELS unit
  TIMEOUT 30)

# The counts that size a parameter list: allocSize and the constructors reject
# a negative count and one RtInt cannot hold. See
# tests/parametercountbounds_test.cpp.
add_executable(gman_parametercountbounds parametercountbounds_test.cpp)
target_link_libraries(gman_parametercountbounds PRIVATE gman)

add_test(NAME parametercountbounds COMMAND gman_parametercountbounds)
set_tests_properties(parametercountbounds PROPERTIES
  LABELS unit
  TIMEOUT 30)

# A shader reads a declared or inline parameter by name from a list built on
# a dictionary of its own, and each accessor answers only its own type and
# length. See tests/shaderparameters_test.cpp.
add_executable(gman_shaderparameters shaderparameters_test.cpp)
target_link_libraries(gman_shaderparameters PRIVATE gman)

add_test(NAME shaderparameters COMMAND gman_shaderparameters)
set_tests_properties(shaderparameters PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANBBox's two-point constructor and its getMin()/getMax() accessors.
# See tests/bbox_test.cpp.
add_executable(gman_bbox bbox_test.cpp)
target_link_libraries(gman_bbox PRIVATE gman)

add_test(NAME bbox COMMAND gman_bbox)
set_tests_properties(bbox PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# gman::boundingBoxExtent, gman::floatArray and gman::polygonTexCoords, as
# gmanpolygon.h declares them. See tests/polygonhelpers_test.cpp.
add_executable(gman_polygonhelpers polygonhelpers_test.cpp)
target_link_libraries(gman_polygonhelpers PRIVATE gman)

add_test(NAME polygonhelpers COMMAND gman_polygonhelpers)
set_tests_properties(polygonhelpers PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANDictionary::getTypeSize's default: arm reports an unknown token type
# truthfully. See tests/dictionaryerror_test.cpp.
add_executable(gman_dictionaryerror dictionaryerror_test.cpp)
target_link_libraries(gman_dictionaryerror PRIVATE gman)

add_test(NAME dictionaryerror COMMAND gman_dictionaryerror)
set_tests_properties(dictionaryerror PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Pins RIE_NOTSTARTED's and RiDeclare's RIE_SYNTAX messages through
# RiErrorHandler.
add_executable(gman_diagnostics diagnostics_test.cpp)
target_link_libraries(gman_diagnostics PRIVATE gman)

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
target_link_libraries(gman_worldnodisplay PRIVATE gman)
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

# GMANRenderManImpl's renderer, viewingSystem and output own nothing, and its
# borrowed primitiveFactory points nowhere, from construction; RiEnd leaves
# them so. See tests/rendermanpointers_test.cpp.
add_executable(gman_rendermanpointers rendermanpointers_test.cpp)
target_link_libraries(gman_rendermanpointers PRIVATE gman gman_internal_headers)
add_dependencies(gman_rendermanpointers gman_zbuffer)

add_test(NAME rendermanpointers
  COMMAND gman_rendermanpointers
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(rendermanpointers PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Two worlds in one cycle, two RiBegins, an impl destroyed before RiEnd and the
# C API's RiBegin/RiEnd cycles, failed RiBegins included, each return the live
# heap to its starting size. The test counts the heap itself, so it is its own
# executable. libgmanrib.so loads for the .rib case, so the test holds the
# plugin lock. See tests/rendermanlifetime_test.cpp.
add_executable(gman_rendermanlifetime rendermanlifetime_test.cpp)
target_link_libraries(gman_rendermanlifetime PRIVATE gman gman_internal_headers)
add_dependencies(gman_rendermanlifetime gman_zbuffer gman_plastic gman_rib)

add_test(NAME rendermanlifetime
  COMMAND gman_rendermanlifetime
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(rendermanlifetime PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30
  RESOURCE_LOCK gmanrib_plugin_file)

# RiWorldEnd rejects a display mode it does not know and closes the world on
# the error. See tests/invaliddisplaymode_test.cpp.
add_executable(gman_invaliddisplaymode invaliddisplaymode_test.cpp)
target_link_libraries(gman_invaliddisplaymode PRIVATE gman gman_internal_headers)
add_dependencies(gman_invaliddisplaymode gman_zbuffer)

add_test(NAME invaliddisplaymode
  COMMAND gman_invaliddisplaymode
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(invaliddisplaymode PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# After a RiBegin whose renderer fails to load, the first request that needs
# one raises RIE_NOTSTARTED once and later requests return quietly. See
# tests/failedbegin_test.cpp.
add_executable(gman_failedbegin failedbegin_test.cpp)
target_link_libraries(gman_failedbegin PRIVATE gman gman_internal_headers)
add_dependencies(gman_failedbegin gman_zbuffer)

add_test(NAME failedbegin
  COMMAND gman_failedbegin
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(failedbegin PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# A C API RiBegin whose context fails to start leaves no context current and
# frees the context, so later requests raise RIE_NOTSTARTED. The current
# context is process-wide, so the test is its own executable. See
# tests/failedbegincapi_test.cpp.
add_executable(gman_failedbegincapi failedbegincapi_test.cpp)
target_link_libraries(gman_failedbegincapi PRIVATE gman)
add_dependencies(gman_failedbegincapi gman_zbuffer gman_rib)

add_test(NAME failedbegincapi
  COMMAND gman_failedbegincapi
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(failedbegincapi PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30
  RESOURCE_LOCK gmanrib_plugin_file)

# RIB dialect coverage. Each spawns the real gman binary, which dlopens
# its renderer by bare name, so each needs the same library path as the
# baseline test above.
set(GMAN_PHASE2_ENV
  "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}")

add_executable(gman_ribdialect ribdialect_test.cpp)
target_link_libraries(gman_ribdialect PRIVATE gman)

set(GMAN_RIBDIALECT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/ribdialect-run")
file(MAKE_DIRECTORY "${GMAN_RIBDIALECT_WORKDIR}")

add_test(NAME ribdialect
  COMMAND gman_ribdialect $<TARGET_FILE:gman_cli>
                          "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_RIBDIALECT_WORKDIR}")
set_tests_properties(ribdialect PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 240)

add_executable(gman_readarchive readarchive_test.cpp)
target_link_libraries(gman_readarchive PRIVATE gman)

set(GMAN_READARCHIVE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/readarchive-run")
file(MAKE_DIRECTORY "${GMAN_READARCHIVE_WORKDIR}")

add_test(NAME readarchive
  COMMAND gman_readarchive $<TARGET_FILE:gman_cli>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib/archive"
  WORKING_DIRECTORY "${GMAN_READARCHIVE_WORKDIR}")
set_tests_properties(readarchive PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 60)

add_executable(gman_gzip gzip_test.cpp)
target_link_libraries(gman_gzip PRIVATE gman)

set(GMAN_GZIP_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/gzip-run")
file(MAKE_DIRECTORY "${GMAN_GZIP_WORKDIR}")

add_test(NAME gzip
  COMMAND gman_gzip $<TARGET_FILE:gman_cli>
                    "${CMAKE_CURRENT_SOURCE_DIR}/rib/gzip"
                    "${GMAN_GZIP_WORKDIR}/scratch"
  WORKING_DIRECTORY "${GMAN_GZIP_WORKDIR}")
set_tests_properties(gzip PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

add_executable(gman_logfilename logfilename_test.cpp)
target_link_libraries(gman_logfilename PRIVATE gman)

set(GMAN_LOGFILENAME_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/logfilename-run")
file(MAKE_DIRECTORY "${GMAN_LOGFILENAME_WORKDIR}")

add_test(NAME logfilename
  COMMAND gman_logfilename $<TARGET_FILE:gman_cli>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere.rib"
                           "${GMAN_LOGFILENAME_WORKDIR}/scratch"
  WORKING_DIRECTORY "${GMAN_LOGFILENAME_WORKDIR}")
set_tests_properties(logfilename PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

add_executable(gman_rflag rflag_test.cpp)
target_link_libraries(gman_rflag PRIVATE gman)

set(GMAN_RFLAG_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/rflag-run")
file(MAKE_DIRECTORY "${GMAN_RFLAG_WORKDIR}")

add_test(NAME rflag
  COMMAND gman_rflag $<TARGET_FILE:gman_cli>
                     "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere.rib"
  WORKING_DIRECTORY "${GMAN_RFLAG_WORKDIR}")
set_tests_properties(rflag PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

add_executable(gman_failedbegincli failedbegincli_test.cpp)
target_link_libraries(gman_failedbegincli PRIVATE gman)

set(GMAN_FAILEDBEGINCLI_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/failedbegincli-run")
file(MAKE_DIRECTORY "${GMAN_FAILEDBEGINCLI_WORKDIR}")

add_test(NAME failedbegincli
  COMMAND gman_failedbegincli $<TARGET_FILE:gman_cli>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere.rib"
  WORKING_DIRECTORY "${GMAN_FAILEDBEGINCLI_WORKDIR}")
set_tests_properties(failedbegincli PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 120)

add_executable(gman_unknownflag unknownflag_test.cpp)
target_link_libraries(gman_unknownflag PRIVATE gman)

set(GMAN_UNKNOWNFLAG_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/unknownflag-run")
file(MAKE_DIRECTORY "${GMAN_UNKNOWNFLAG_WORKDIR}")

add_test(NAME unknownflag
  COMMAND gman_unknownflag $<TARGET_FILE:gman_cli>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere.rib"
  WORKING_DIRECTORY "${GMAN_UNKNOWNFLAG_WORKDIR}")
set_tests_properties(unknownflag PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

add_executable(gman_facevarying facevarying_test.cpp)
target_link_libraries(gman_facevarying PRIVATE gman)

add_test(NAME facevarying COMMAND gman_facevarying)
set_tests_properties(facevarying PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_bitmap bitmap_test.cpp)
target_link_libraries(gman_bitmap PRIVATE gman)

add_test(NAME bitmap COMMAND gman_bitmap)
set_tests_properties(bitmap PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_inlineparsewordbound inlineparsewordbound_test.cpp)
target_link_libraries(gman_inlineparsewordbound PRIVATE gman gman_internal_headers)

add_test(NAME inlineparsewordbound COMMAND gman_inlineparsewordbound)
set_tests_properties(inlineparsewordbound PROPERTIES
  LABELS unit
  TIMEOUT 30)

# The object -> world -> camera -> screen -> NDC -> raster chain.
add_executable(gman_matrix4 gmanmatrix4_test.cpp)
target_link_libraries(gman_matrix4 PRIVATE gman)
add_test(NAME matrix4 COMMAND gman_matrix4)
set_tests_properties(matrix4 PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_matrixscalarmultiply matrixscalarmultiply_test.cpp)
target_link_libraries(gman_matrixscalarmultiply PRIVATE gman)
add_test(NAME matrixscalarmultiply COMMAND gman_matrixscalarmultiply)
set_tests_properties(matrixscalarmultiply PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_geometrytemplates geometrytemplates_test.cpp)
target_link_libraries(gman_geometrytemplates PRIVATE gman)
add_test(NAME geometrytemplates COMMAND gman_geometrytemplates)
set_tests_properties(geometrytemplates PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_vectortemplate vectortemplate_test.cpp)
target_link_libraries(gman_vectortemplate PRIVATE gman)
add_test(NAME vectortemplate COMMAND gman_vectortemplate)
set_tests_properties(vectortemplate PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_pointtemplate pointtemplate_test.cpp)
target_link_libraries(gman_pointtemplate PRIVATE gman)
add_test(NAME pointtemplate COMMAND gman_pointtemplate)
set_tests_properties(pointtemplate PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Constructing a Point from an HPoint mutates the HPoint. Pins the wanted
# behavior; WILL_FAIL until the argument is left unchanged.
add_executable(gman_pointfromhpoint_pin pointfromhpoint_pin_test.cpp)
target_link_libraries(gman_pointfromhpoint_pin PRIVATE gman)
add_test(NAME pointfromhpoint_pin COMMAND gman_pointfromhpoint_pin)
set_tests_properties(pointfromhpoint_pin PROPERTIES
  WILL_FAIL TRUE
  LABELS unit
  TIMEOUT 30)

add_executable(gman_normaltemplate normaltemplate_test.cpp)
target_link_libraries(gman_normaltemplate PRIVATE gman)
add_test(NAME normaltemplate COMMAND gman_normaltemplate)
set_tests_properties(normaltemplate PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_subdivisiontag subdivisiontag_test.cpp)
target_link_libraries(gman_subdivisiontag PRIVATE gman)
add_test(NAME subdivisiontag COMMAND gman_subdivisiontag)
set_tests_properties(subdivisiontag PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_noisedeterminism noisedeterminism_test.cpp)
target_link_libraries(gman_noisedeterminism PRIVATE gman gman_internal_headers)
add_test(NAME noisedeterminism COMMAND gman_noisedeterminism)
set_tests_properties(noisedeterminism PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Every noise, periodic and cellnoise form answers NaN for a non-finite
# coordinate or an unusable period, and the lattice's periodic value for a
# finite coordinate beyond RtInt's range. See tests/noisenonfinite_test.cpp.
add_executable(gman_noisenonfinite noisenonfinite_test.cpp)
target_link_libraries(gman_noisenonfinite PRIVATE gman gman_internal_headers)
add_test(NAME noisenonfinite COMMAND gman_noisenonfinite)
set_tests_properties(noisenonfinite PROPERTIES
  LABELS unit
  TIMEOUT 30)

# A vertex the clipper introduces at a plane crossing carries the colour and
# alpha interpolated at the crossing, at hand-derived values.
add_executable(gman_clipcolor clipcolor_test.cpp)
target_link_libraries(gman_clipcolor PRIVATE gman gman_raster gman_internal_headers)
add_test(NAME clipcolor COMMAND gman_clipcolor)
set_tests_properties(clipcolor PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_spacechain spacechain_test.cpp)
target_link_libraries(gman_spacechain PRIVATE gman gman_raster gman_internal_headers)
add_test(NAME spacechain COMMAND gman_spacechain)
set_tests_properties(spacechain PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_facescamera facescamera_test.cpp)
target_link_libraries(gman_facescamera PRIVATE gman gman_internal_headers)
add_test(NAME facescamera COMMAND gman_facescamera)
set_tests_properties(facescamera PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_viewingsystemfactory viewingsystemfactory_test.cpp)
target_link_libraries(gman_viewingsystemfactory PRIVATE gman gman_internal_headers)
add_test(NAME viewingsystemfactory COMMAND gman_viewingsystemfactory)
set_tests_properties(viewingsystemfactory PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Analytic primitive normals: finite-difference agreement,
# cross-primitive orientation consistency, inverse-transpose under a
# non-uniform scale.
add_executable(gman_normals normals_test.cpp)
target_link_libraries(gman_normals PRIVATE gman gman_raster)
add_test(NAME normals COMMAND gman_normals)
# The inverse-transpose proof (item 3) tessellates a real sphere through
# GMANRasterPrimitiveFactory, which falls back to dlopen'ing "libmatte.so"
# when no RiSurface is set -- the same bare-name lookup gman itself relies
# on the build tree being on the runtime search path for.
set_tests_properties(normals PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Scale- and placement-invariance of
# GMANRasterPrimitiveFactory::getRSPolygonMesh's ear-clipping
# classification: nine hand-built rings at six scales in two placements, a
# randomized star-shaped stress, and degenerate input.
add_executable(gman_triangulation triangulation_test.cpp)
target_link_libraries(gman_triangulation PRIVATE gman gman_raster gman_internal_headers)
add_test(NAME triangulation COMMAND gman_triangulation)
# getRSPolygonMesh falls back to dlopen'ing "libmatte.so" when no RiSurface
# is set, the same bare-name lookup gman itself relies on the build tree
# being on the runtime search path for.
set_tests_properties(triangulation PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GeneralPolygon hole bridging: getRSPolygonMesh merges each hole into the
# outer loop by Eberly's method before handing the ring to the same
# triangulateEarClipping loop 1 already uses. See tests/generalpolygon_test.cpp.
add_executable(gman_generalpolygon generalpolygon_test.cpp)
target_link_libraries(gman_generalpolygon PRIVATE gman gman_raster gman_internal_headers)
add_test(NAME generalpolygon COMMAND gman_generalpolygon)
# getRSPolygonMesh falls back to dlopen'ing "libmatte.so" when no
# RiSurface is set, the same bare-name lookup gman itself relies on the
# build tree being on the runtime search path for.
set_tests_properties(generalpolygon PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The actual gate: a real end-to-end render, read back and measured
# against the hand-derived silhouette geometry.
add_executable(gman_silhouette silhouette_test.cpp)
target_link_libraries(gman_silhouette PRIVATE gman TIFF::TIFF)

set(GMAN_SILHOUETTE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/silhouette-run")
file(MAKE_DIRECTORY "${GMAN_SILHOUETTE_WORKDIR}")

add_test(NAME silhouette
  COMMAND gman_silhouette $<TARGET_FILE:gman_cli>
  WORKING_DIRECTORY "${GMAN_SILHOUETTE_WORKDIR}")
set_tests_properties(silhouette PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# An in-world Rotate must compose ahead of the transforms declared after it,
# about the world origin, not the camera (GMANGraphicState::buildTransform).
add_executable(gman_rotate rotate_test.cpp)
target_link_libraries(gman_rotate PRIVATE gman TIFF::TIFF)

set(GMAN_ROTATE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/rotate-run")
file(MAKE_DIRECTORY "${GMAN_ROTATE_WORKDIR}")

add_test(NAME rotate
  COMMAND gman_rotate $<TARGET_FILE:gman_cli>
                      "${CMAKE_CURRENT_SOURCE_DIR}/rib/rotate.rib"
  WORKING_DIRECTORY "${GMAN_ROTATE_WORKDIR}")
set_tests_properties(rotate PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Proof item 6 (secondary, visual): the same sphere at several RiTranslate
# offsets.
add_executable(gman_transforms transforms_test.cpp)
target_link_libraries(gman_transforms PRIVATE gman TIFF::TIFF)

set(GMAN_TRANSFORMS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/transforms-run")
file(MAKE_DIRECTORY "${GMAN_TRANSFORMS_WORKDIR}")

add_test(NAME transforms
  COMMAND gman_transforms $<TARGET_FILE:gman_cli>
                          "${CMAKE_CURRENT_SOURCE_DIR}/rib/transforms.rib"
  WORKING_DIRECTORY "${GMAN_TRANSFORMS_WORKDIR}")
set_tests_properties(transforms PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Proof items 4-6: the N.L=0 terminator, RiSides 1 vs RiSides 2, and the
# golden image (tests/rib/lights.rib against tests/rib/lights_golden.tif).
add_executable(gman_lighting lighting_test.cpp)
target_link_libraries(gman_lighting PRIVATE gman TIFF::TIFF)

set(GMAN_LIGHTING_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/lighting-run")
file(MAKE_DIRECTORY "${GMAN_LIGHTING_WORKDIR}")

add_test(NAME lighting
  COMMAND gman_lighting $<TARGET_FILE:gman_cli>
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
target_link_libraries(gman_spotlight PRIVATE gman TIFF::TIFF)

set(GMAN_SPOTLIGHT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/spotlight-run")
file(MAKE_DIRECTORY "${GMAN_SPOTLIGHT_WORKDIR}")

add_test(NAME spotlight
  COMMAND gman_spotlight $<TARGET_FILE:gman_cli>
                         "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_SPOTLIGHT_WORKDIR}")
set_tests_properties(spotlight PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Step 3/5: all seven quadric primitives in one scene, golden-image
# regression via the shared harness (tests/goldenimage.h).
add_executable(gman_quadrics quadrics_test.cpp)
target_link_libraries(gman_quadrics PRIVATE gman TIFF::TIFF)

set(GMAN_QUADRICS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/quadrics-run")
file(MAKE_DIRECTORY "${GMAN_QUADRICS_WORKDIR}")

add_test(NAME quadrics
  COMMAND gman_quadrics $<TARGET_FILE:gman_cli>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_QUADRICS_WORKDIR}")
set_tests_properties(quadrics PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Step 3/5: RiScreenWindow + RiCropWindow together under perspective, plus
# an unrecognized RiFrameAspectRatio proving the skip path does not desync.
add_executable(gman_screenwindow screenwindow_test.cpp)
target_link_libraries(gman_screenwindow PRIVATE gman TIFF::TIFF)

set(GMAN_SCREENWINDOW_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/screenwindow-run")
file(MAKE_DIRECTORY "${GMAN_SCREENWINDOW_WORKDIR}")

add_test(NAME screenwindow
  COMMAND gman_screenwindow $<TARGET_FILE:gman_cli>
                            "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_SCREENWINDOW_WORKDIR}")
set_tests_properties(screenwindow PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Step 3/5: a cylinder straddling both the near and the far clip plane.
add_executable(gman_clipping clipping_test.cpp)
target_link_libraries(gman_clipping PRIVATE gman TIFF::TIFF)

set(GMAN_CLIPPING_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/clipping-run")
file(MAKE_DIRECTORY "${GMAN_CLIPPING_WORKDIR}")

add_test(NAME clipping
  COMMAND gman_clipping $<TARGET_FILE:gman_cli>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_CLIPPING_WORKDIR}")
set_tests_properties(clipping PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Step 3/5: matte, plastic and metal side by side under identical lighting.
add_executable(gman_shaders shaders_test.cpp)
target_link_libraries(gman_shaders PRIVATE gman TIFF::TIFF)

set(GMAN_SHADERS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/shaders-run")
file(MAKE_DIRECTORY "${GMAN_SHADERS_WORKDIR}")

add_test(NAME shaders
  COMMAND gman_shaders $<TARGET_FILE:gman_cli>
                       "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_SHADERS_WORKDIR}")
set_tests_properties(shaders PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# RIB's Quantize keyword, end to end: parsed, honoured by
# GMANOutput::save and reflected in the bytes TIFF and PNM write, a request
# a driver cannot store stopping the file with an error, and a zero "one"
# writing unclamped 32-bit float to TIFF and stopping the file for PNM.
add_executable(gman_quantize quantize_test.cpp)
target_link_libraries(gman_quantize PRIVATE gman TIFF::TIFF)

set(GMAN_QUANTIZE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/quantize-run")
file(MAKE_DIRECTORY "${GMAN_QUANTIZE_WORKDIR}")

add_test(NAME quantize
  COMMAND gman_quantize $<TARGET_FILE:gman_cli>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_QUANTIZE_WORKDIR}")
set_tests_properties(quantize PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# The sample depth of file output, chosen by gman --bit-depth or by Quantize
# "rgba", end to end: 16-bit PNM, PNG and TIFF, an explicit Quantize that
# contradicts the flag stopping the file, a JPEG refusing 16 bits and
# OpenEXR ignoring the flag. The PNG reads compile only under GMAN_WITH_PNG.
add_executable(gman_bitdepth bitdepth_test.cpp)
target_link_libraries(gman_bitdepth PRIVATE TIFF::TIFF)
if(GMAN_WITH_PNG)
  target_compile_definitions(gman_bitdepth PRIVATE GMAN_WITH_PNG)
  target_link_libraries(gman_bitdepth PRIVATE PNG::PNG)
endif()

set(GMAN_BITDEPTH_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/bitdepth-run")
file(MAKE_DIRECTORY "${GMAN_BITDEPTH_WORKDIR}")

add_test(NAME bitdepth
  COMMAND gman_bitdepth $<TARGET_FILE:gman_cli>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_BITDEPTH_WORKDIR}")
set_tests_properties(bitdepth PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

add_executable(gman_colorquantizeset colorquantizeset_test.cpp)
target_link_libraries(gman_colorquantizeset PRIVATE gman)

add_test(NAME colorquantizeset COMMAND gman_colorquantizeset)
set_tests_properties(colorquantizeset PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Step 4: RIB tokenizer robustness on malformed input -- an unterminated
# string, an unbalanced bracket, a file truncated mid-keyword -- each
# under its own hard timeout (see tests/ribmalformed_test.cpp).
add_executable(gman_ribmalformed ribmalformed_test.cpp)
target_link_libraries(gman_ribmalformed PRIVATE gman)

add_test(NAME ribmalformed
  COMMAND gman_ribmalformed $<TARGET_FILE:gman_cli>
                            "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed")
set_tests_properties(ribmalformed PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# Guards against a RIB-supplied parameter array shorter than its declared
# class reading past its own allocation instead of stopping the file with
# RIE_CONSISTENCY. See tests/paramclamp_test.cpp.
add_executable(gman_paramclamp paramclamp_test.cpp)
target_link_libraries(gman_paramclamp PRIVATE gman TIFF::TIFF)

set(GMAN_PARAMCLAMP_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/paramclamp-run")
file(MAKE_DIRECTORY "${GMAN_PARAMCLAMP_WORKDIR}")

add_test(NAME paramclamp
  COMMAND gman_paramclamp $<TARGET_FILE:gman_cli>
                          "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed"
  WORKING_DIRECTORY "${GMAN_PARAMCLAMP_WORKDIR}")
set_tests_properties(paramclamp PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# A RIB integer narrows twice, unchecked, on its way to RtInt: the
# tokenizer's strtol, then nextInt/toRtIntVector's own implicit
# conversion. See tests/ribintegerbounds_test.cpp.
add_executable(gman_ribintegerbounds ribintegerbounds_test.cpp)
target_link_libraries(gman_ribintegerbounds PRIVATE gman)

add_test(NAME ribintegerbounds
  COMMAND gman_ribintegerbounds $<TARGET_FILE:gman_cli>
                                 "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed")
set_tests_properties(ribintegerbounds PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# A RIB request whose declared counts no RtInt holds, or whose arrays fall
# short of them, stops the file instead of allocating for it. Each run writes
# malformed_out.tif into its working directory. See
# tests/ribcountbounds_test.cpp.
add_executable(gman_ribcountbounds ribcountbounds_test.cpp)
target_link_libraries(gman_ribcountbounds PRIVATE gman TIFF::TIFF)

set(GMAN_RIBCOUNTBOUNDS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/ribcountbounds-run")
file(MAKE_DIRECTORY "${GMAN_RIBCOUNTBOUNDS_WORKDIR}")

add_test(NAME ribcountbounds
  COMMAND gman_ribcountbounds $<TARGET_FILE:gman_cli>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed"
  WORKING_DIRECTORY "${GMAN_RIBCOUNTBOUNDS_WORKDIR}")
set_tests_properties(ribcountbounds PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# Polygon and the seven quadrics built their GMANParameterList with no
# suppliedCounts at all, so a short array read past its own allocation
# instead of stopping the file with RIE_CONSISTENCY. See
# tests/geometryparamclamp_test.cpp.
add_executable(gman_geometryparamclamp geometryparamclamp_test.cpp)
target_link_libraries(gman_geometryparamclamp PRIVATE gman TIFF::TIFF)

set(GMAN_GEOMETRYPARAMCLAMP_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/geometryparamclamp-run")
file(MAKE_DIRECTORY "${GMAN_GEOMETRYPARAMCLAMP_WORKDIR}")

add_test(NAME geometryparamclamp
  COMMAND gman_geometryparamclamp $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed"
  WORKING_DIRECTORY "${GMAN_GEOMETRYPARAMCLAMP_WORKDIR}")
set_tests_properties(geometryparamclamp PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# A RIB-supplied parameter array longer than its declared class warns and
# truncates. See tests/paramoversupply_test.cpp.
add_executable(gman_paramoversupply paramoversupply_test.cpp)
target_link_libraries(gman_paramoversupply PRIVATE gman TIFF::TIFF)

set(GMAN_PARAMOVERSUPPLY_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/paramoversupply-run")
file(MAKE_DIRECTORY "${GMAN_PARAMOVERSUPPLY_WORKDIR}")

add_test(NAME paramoversupply
  COMMAND gman_paramoversupply $<TARGET_FILE:gman_cli>
                               "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed"
  WORKING_DIRECTORY "${GMAN_PARAMOVERSUPPLY_WORKDIR}")
set_tests_properties(paramoversupply PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# Guards against a declared-integer parameter being stored as RtFloat and
# read back through an (RtInt*) cast -- float bits reinterpreted as an
# integer, not precision loss. See tests/paramlistdeclaredtype_test.cpp;
# its own final check drives GMANRIBParse in process (RiBegin dlopens the
# default renderer by bare name, hence the same
# LD_LIBRARY_PATH/DYLD_LIBRARY_PATH as tests/basicstate.c).
add_executable(gman_paramlistdeclaredtype paramlistdeclaredtype_test.cpp)
target_link_libraries(gman_paramlistdeclaredtype PRIVATE gman gman_internal_headers)

add_test(NAME paramlistdeclaredtype
  COMMAND gman_paramlistdeclaredtype $<TARGET_FILE:gman_cli>
                                      "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed")
set_tests_properties(paramlistdeclaredtype PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# A RIB parameter whose literal kind contradicts its declared type stops the
# file with RIE_CONSISTENCY. See tests/paramliteraltype_test.cpp and
# tests/paramrejection.h; its own final check drives GMANRIBParse in process,
# hence gman_internal_headers and the same LD_LIBRARY_PATH/DYLD_LIBRARY_PATH as
# tests/basicstate.c.
add_executable(gman_paramliteraltype paramliteraltype_test.cpp)
target_link_libraries(gman_paramliteraltype PRIVATE gman gman_internal_headers TIFF::TIFF)

set(GMAN_PARAMLITERALTYPE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/paramliteraltype-run")
file(MAKE_DIRECTORY "${GMAN_PARAMLITERALTYPE_WORKDIR}")

add_test(NAME paramliteraltype
  COMMAND gman_paramliteraltype $<TARGET_FILE:gman_cli>
                      "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed"
  WORKING_DIRECTORY "${GMAN_PARAMLITERALTYPE_WORKDIR}")
set_tests_properties(paramliteraltype PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# RIB's PixelVariance reaches GMANOptions, which reads 0 when none is set. See
# tests/ribpixelvariance_test.cpp.
add_executable(gman_ribpixelvariance ribpixelvariance_test.cpp)
target_link_libraries(gman_ribpixelvariance PRIVATE gman gman_internal_headers)

add_test(NAME ribpixelvariance
  COMMAND gman_ribpixelvariance "${CMAKE_CURRENT_SOURCE_DIR}/rib")
set_tests_properties(ribpixelvariance PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# A RIB parameter supplied with fewer values than one element of its declared
# type stops the file with RIE_CONSISTENCY. See tests/paramshortcount_test.cpp.
add_executable(gman_paramshortcount paramshortcount_test.cpp)
target_link_libraries(gman_paramshortcount PRIVATE gman TIFF::TIFF)

set(GMAN_PARAMSHORTCOUNT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/paramshortcount-run")
file(MAKE_DIRECTORY "${GMAN_PARAMSHORTCOUNT_WORKDIR}")

add_test(NAME paramshortcount
  COMMAND gman_paramshortcount $<TARGET_FILE:gman_cli>
                      "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed"
  WORKING_DIRECTORY "${GMAN_PARAMSHORTCOUNT_WORKDIR}")
set_tests_properties(paramshortcount PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# A RIB parameter declaring more values than an int can count stops the file
# with RIE_CONSISTENCY. See tests/paramhugecount_test.cpp.
add_executable(gman_paramhugecount paramhugecount_test.cpp)
target_link_libraries(gman_paramhugecount PRIVATE gman TIFF::TIFF)

set(GMAN_PARAMHUGECOUNT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/paramhugecount-run")
file(MAKE_DIRECTORY "${GMAN_PARAMHUGECOUNT_WORKDIR}")

add_test(NAME paramhugecount
  COMMAND gman_paramhugecount $<TARGET_FILE:gman_cli>
                      "${CMAKE_CURRENT_SOURCE_DIR}/rib/malformed"
  WORKING_DIRECTORY "${GMAN_PARAMHUGECOUNT_WORKDIR}")
set_tests_properties(paramhugecount PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# GMANRIBTokenize::parseString gained escape decoding so an embedded \"
# stops desyncing the parse. See tests/ribstringescape_test.cpp; its own
# in-process check drives GMANRIBParse directly, hence the same
# LD_LIBRARY_PATH/DYLD_LIBRARY_PATH as tests/basicstate.c.
add_executable(gman_ribstringescape ribstringescape_test.cpp)
target_link_libraries(gman_ribstringescape PRIVATE gman gman_internal_headers)

add_test(NAME ribstringescape
  COMMAND gman_ribstringescape $<TARGET_FILE:gman_cli>
                               "${CMAKE_CURRENT_SOURCE_DIR}/rib")
set_tests_properties(ribstringescape PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# GMANRIBParse::parsePoints derives npoints from "P"'s float count. See
# tests/pointscount_test.cpp; its in-process check drives GMANRIBParse
# directly against a CapturingRenderMan overriding RiPointsV, the same
# technique tests/paramlistdeclaredtype_test.cpp's check 5 and
# tests/ribstringescape_test.cpp use, hence the same
# LD_LIBRARY_PATH/DYLD_LIBRARY_PATH as tests/basicstate.c.
add_executable(gman_pointscount pointscount_test.cpp)
target_link_libraries(gman_pointscount PRIVATE gman gman_internal_headers)

add_test(NAME pointscount
  COMMAND gman_pointscount "${CMAKE_CURRENT_SOURCE_DIR}/rib")
set_tests_properties(pointscount PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANRIBParse falls back to the RI-mandated overload of 14 requests when
# renderMan is not a GMANRenderManImpl. See tests/ribparsefallback_test.cpp;
# its in-process check hands the parser a GMANASCII, so gmanascii.cpp is
# compiled directly into this executable rather than dlopened, needing the
# same LD_LIBRARY_PATH/DYLD_LIBRARY_PATH as tests/basicstate.c.
add_executable(gman_ribparsefallback ribparsefallback_test.cpp
  "${CMAKE_SOURCE_DIR}/libgmanrib/gmanascii.cpp")
target_include_directories(gman_ribparsefallback PRIVATE "${CMAKE_SOURCE_DIR}/libgmanrib")
target_link_libraries(gman_ribparsefallback PRIVATE gman gman_internal_headers)

add_test(NAME ribparsefallback
  COMMAND gman_ribparsefallback "${CMAKE_CURRENT_SOURCE_DIR}/rib")
set_tests_properties(ribparsefallback PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# ColorSamples sets the number of floats Color and Opacity carry; GMAN shades
# in three-component RGB only. See tests/colorsamples_test.cpp.
add_executable(gman_colorsamples colorsamples_test.cpp
  "${CMAKE_SOURCE_DIR}/libgmanrib/gmanascii.cpp")
target_include_directories(gman_colorsamples PRIVATE "${CMAKE_SOURCE_DIR}/libgmanrib")
target_link_libraries(gman_colorsamples PRIVATE gman gman_internal_headers)

add_test(NAME colorsamples
  COMMAND gman_colorsamples "${CMAKE_CURRENT_SOURCE_DIR}/rib")
set_tests_properties(colorsamples PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANASCII writes Color and Opacity at the ColorSamples count. See tests/ribwritercolorcount_test.cpp.
add_executable(gman_ribwritercolorcount ribwritercolorcount_test.cpp
  "${CMAKE_SOURCE_DIR}/libgmanrib/gmanascii.cpp")
target_include_directories(gman_ribwritercolorcount PRIVATE "${CMAKE_SOURCE_DIR}/libgmanrib")
target_link_libraries(gman_ribwritercolorcount PRIVATE gman gman_internal_headers)

add_test(NAME ribwritercolorcount COMMAND gman_ribwritercolorcount)
set_tests_properties(ribwritercolorcount PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# A Transform, ConcatTransform or inline Basis matrix of other than 16 values
# is RIE_CONSISTENCY; a non-number in one is RIE_SYNTAX. See
# tests/ribmatrixlength_test.cpp.
add_executable(gman_ribmatrixlength ribmatrixlength_test.cpp
  "${CMAKE_SOURCE_DIR}/libgmanrib/gmanascii.cpp")
target_include_directories(gman_ribmatrixlength PRIVATE "${CMAKE_SOURCE_DIR}/libgmanrib")
target_link_libraries(gman_ribmatrixlength PRIVATE gman gman_internal_headers)

add_test(NAME ribmatrixlength
  COMMAND gman_ribmatrixlength "${CMAKE_CURRENT_SOURCE_DIR}/rib")
set_tests_properties(ribmatrixlength PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANRIBTokenize::consumeWhitespace, isKeyToken and isNumToken classify a
# stream byte. See tests/ribctypeunsigned_test.cpp; it drives getNext
# directly on an istringstream, so it dlopens nothing.
add_executable(gman_ribctypeunsigned ribctypeunsigned_test.cpp)
target_link_libraries(gman_ribctypeunsigned PRIVATE gman gman_internal_headers)

add_test(NAME ribctypeunsigned COMMAND gman_ribctypeunsigned)
set_tests_properties(ribctypeunsigned PROPERTIES LABELS unit TIMEOUT 30)

# Bilinear and bicubic Patch rasterization. See tests/patchnorender_test.cpp.
add_executable(gman_patchnorender patchnorender_test.cpp)
target_link_libraries(gman_patchnorender PRIVATE gman TIFF::TIFF)

set(GMAN_PATCHNORENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/patchnorender-run")
file(MAKE_DIRECTORY "${GMAN_PATCHNORENDER_WORKDIR}")

add_test(NAME patchnorender
  COMMAND gman_patchnorender $<TARGET_FILE:gman_cli>
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
target_link_libraries(gman_patchmesh PRIVATE gman gman_raster TIFF::TIFF)

set(GMAN_PATCHMESH_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/patchmesh-run")
file(MAKE_DIRECTORY "${GMAN_PATCHMESH_WORKDIR}")

add_test(NAME patchmesh
  COMMAND gman_patchmesh $<TARGET_FILE:gman_cli>
                         "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_PATCHMESH_WORKDIR}")
set_tests_properties(patchmesh PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

# No other test renders a genuinely asymmetric partial sphere's image
# (transforms.rib's Sphere 0.5 -0.5 0.5 360 is a full sphere). This is
# the end-to-end proof of GMANSphere::getLocation's zmin/zmax handling,
# and of the Clipping workaround the near-clip precision defect requires
# for this shape.
add_executable(gman_partialsphere partialsphere_test.cpp)
target_link_libraries(gman_partialsphere PRIVATE gman TIFF::TIFF)

set(GMAN_PARTIALSPHERE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/partialsphere-run")
file(MAKE_DIRECTORY "${GMAN_PARTIALSPHERE_WORKDIR}")

add_test(NAME partialsphere
  COMMAND gman_partialsphere $<TARGET_FILE:gman_cli>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_PARTIALSPHERE_WORKDIR}")
set_tests_properties(partialsphere PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# RISpec pixel-reconstruction filters (box, triangle, gaussian,
# catmull-rom, sinc): a pure unit test on gmanfilters.cpp, no gman binary.
# See tests/filters_test.cpp for which of the five were wrong and why.
add_executable(gman_filters filters_test.cpp)
target_link_libraries(gman_filters PRIVATE gman)
add_test(NAME filters COMMAND gman_filters)
set_tests_properties(filters PROPERTIES
  LABELS unit
  TIMEOUT 30)

# OutputTIFF::save ignored its DisplayMode and always wrote 4 samples with
# no ExtraSamples tag. Direct unit test against gman::OutputTIFF, no gman
# binary: "rgb" writes 3 samples and no tag, "rgba" keeps 4 and tags the
# fourth EXTRASAMPLE_ASSOCALPHA.
add_executable(gman_tiffdisplaymode tiffdisplaymode_test.cpp)
target_link_libraries(gman_tiffdisplaymode PRIVATE gman TIFF::TIFF gman_internal_headers)

set(GMAN_TIFFDISPLAYMODE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/tiffdisplaymode-run")
file(MAKE_DIRECTORY "${GMAN_TIFFDISPLAYMODE_WORKDIR}")

add_test(NAME tiffdisplaymode
  COMMAND gman_tiffdisplaymode
  WORKING_DIRECTORY "${GMAN_TIFFDISPLAYMODE_WORKDIR}")
set_tests_properties(tiffdisplaymode PROPERTIES
  LABELS unit
  TIMEOUT 30)

# A Projection with no explicit "fov" defaults to 90 per RISpec 3.2,
# rather than throwing.
add_executable(gman_fovdefault fovdefault_test.cpp)
target_link_libraries(gman_fovdefault PRIVATE gman TIFF::TIFF)

set(GMAN_FOVDEFAULT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/fovdefault-run")
file(MAKE_DIRECTORY "${GMAN_FOVDEFAULT_WORKDIR}")

add_test(NAME fovdefault
  COMMAND gman_fovdefault $<TARGET_FILE:gman_cli>
  WORKING_DIRECTORY "${GMAN_FOVDEFAULT_WORKDIR}")
set_tests_properties(fovdefault PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Guards against Surface before AttributeBegin/FrameBegin double-freeing
# a dlopen'd shader module on the attributesStack copy both requests make.
add_executable(gman_attributescopy attributescopy_test.cpp)
target_link_libraries(gman_attributescopy PRIVATE gman TIFF::TIFF)

set(GMAN_ATTRIBUTESCOPY_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/attributescopy-run")
file(MAKE_DIRECTORY "${GMAN_ATTRIBUTESCOPY_WORKDIR}")

add_test(NAME attributescopy
  COMMAND gman_attributescopy $<TARGET_FILE:gman_cli>
  WORKING_DIRECTORY "${GMAN_ATTRIBUTESCOPY_WORKDIR}")
set_tests_properties(attributescopy PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Guards against the PNG output driver segfaulting: a read-side libpng
# call on a write struct, a mis-sized pixel buffer, or a loop that only
# ever writes column 0; and against PNG colour that is not straight alpha,
# whose colour times its alpha must reconstruct the premultiplied colour a
# TIFF of the same scene holds. The binary needs libpng to link, so it stays
# guarded; the test itself registers unconditionally so a build without
# libpng reports the gap as Skipped rather than making the PNG driver
# invisible to ctest.
if(GMAN_WITH_PNG)
  add_executable(gman_pngdriver pngdriver_test.cpp)
  target_link_libraries(gman_pngdriver PRIVATE gman PNG::PNG TIFF::TIFF)

  set(GMAN_PNGDRIVER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/pngdriver-run")
  file(MAKE_DIRECTORY "${GMAN_PNGDRIVER_WORKDIR}")

  set(GMAN_PNGDRIVER_COMMAND gman_pngdriver $<TARGET_FILE:gman_cli>)
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

# Guards against .jpg/.jpeg having no Display extension dispatch branch,
# which would leave newOutput null and RiWorldEnd dereferencing it
# unconditionally. The binary needs libjpeg to link, so it stays guarded;
# the test itself registers unconditionally so a build without libjpeg
# reports the gap as Skipped rather than making the JPEG driver invisible
# to ctest.
if(GMAN_WITH_JPEG)
  add_executable(gman_jpegdriver jpegdriver_test.cpp)
  target_link_libraries(gman_jpegdriver PRIVATE gman JPEG::JPEG TIFF::TIFF)

  set(GMAN_JPEGDRIVER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/jpegdriver-run")
  file(MAKE_DIRECTORY "${GMAN_JPEGDRIVER_WORKDIR}")

  set(GMAN_JPEGDRIVER_COMMAND gman_jpegdriver $<TARGET_FILE:gman_cli>)
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

if(GMAN_WITH_EXR)
  add_executable(gman_exrdriver exrdriver_test.cpp)
  target_link_libraries(gman_exrdriver PRIVATE gman gman_internal_headers OpenEXR::OpenEXR)
  add_dependencies(gman_exrdriver gman_background gman_matte gman_zbuffer)

  set(GMAN_EXRDRIVER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/exrdriver-run")
  file(MAKE_DIRECTORY "${GMAN_EXRDRIVER_WORKDIR}")

  set(GMAN_EXRDRIVER_COMMAND gman_exrdriver $<TARGET_FILE:gman_cli> "${CMAKE_CURRENT_SOURCE_DIR}/rib")
else()
  set(GMAN_EXRDRIVER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}")
  set(GMAN_EXRDRIVER_COMMAND sh -c "exit 77")
endif()

add_test(NAME exrdriver
  COMMAND ${GMAN_EXRDRIVER_COMMAND}
  WORKING_DIRECTORY "${GMAN_EXRDRIVER_WORKDIR}")
set_tests_properties(exrdriver PROPERTIES
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
if(GMAN_WITH_EXR)
  string(APPEND GMAN_EXPECTED_DRIVERS " exr")
endif()

add_executable(gman_filedrivers filedrivers_test.cpp)
target_link_libraries(gman_filedrivers PRIVATE gman)

set(GMAN_FILEDRIVERS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/filedrivers-run")
file(MAKE_DIRECTORY "${GMAN_FILEDRIVERS_WORKDIR}")

add_test(NAME filedrivers
  COMMAND gman_filedrivers $<TARGET_FILE:gman_cli> "${GMAN_EXPECTED_DRIVERS}"
  WORKING_DIRECTORY "${GMAN_FILEDRIVERS_WORKDIR}")
set_tests_properties(filedrivers PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Pins gman's one-line startup banner against a version-bump regression.
add_executable(gman_banner banner_test.cpp)
target_link_libraries(gman_banner PRIVATE gman)

set(GMAN_BANNER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/banner-run")
file(MAKE_DIRECTORY "${GMAN_BANNER_WORKDIR}")

add_test(NAME banner
  COMMAND gman_banner $<TARGET_FILE:gman_cli>
                       "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere.rib"
                       "${PROJECT_VERSION}"
  WORKING_DIRECTORY "${GMAN_BANNER_WORKDIR}")
set_tests_properties(banner PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

add_executable(gman_commandline commandline_test.cpp)
target_link_libraries(gman_commandline PRIVATE gman_commandline_module)

add_test(NAME commandline COMMAND gman_commandline)
set_tests_properties(commandline PROPERTIES
  LABELS unit
  TIMEOUT 30)

add_executable(gman_commandlinerun commandlinerun_test.cpp)
target_link_libraries(gman_commandlinerun PRIVATE gman_commandline_module)

set(GMAN_COMMANDLINERUN_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/commandlinerun-run")
file(MAKE_DIRECTORY "${GMAN_COMMANDLINERUN_WORKDIR}")

add_test(NAME commandlinerun
  COMMAND gman_commandlinerun $<TARGET_FILE:gman_cli>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib/sphere.rib"
  WORKING_DIRECTORY "${GMAN_COMMANDLINERUN_WORKDIR}")
set_tests_properties(commandlinerun PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 120)

# A known but disabled driver's extension must fail loudly (RIE_BADFILE,
# naming the missing library), not silently. Registered unconditionally
# and always compiled -- it reads which drivers exist from gman --version
# at runtime instead of a compile-time #ifdef, so it runs unchanged across
# the ON, OFF and mixed configurations. It exercises each driver the build
# left out, so the default configure runs the EXR case and drivers-off runs
# all three; with every optional driver enabled there is nothing disabled to
# exercise, so it self-skips (exit 77).
add_executable(gman_displaydisabled displaydisabled_test.cpp)
target_link_libraries(gman_displaydisabled PRIVATE gman)

set(GMAN_DISPLAYDISABLED_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/displaydisabled-run")
file(MAKE_DIRECTORY "${GMAN_DISPLAYDISABLED_WORKDIR}")

add_test(NAME displaydisabled
  COMMAND gman_displaydisabled $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_DISPLAYDISABLED_WORKDIR}")
set_tests_properties(displaydisabled PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30
  SKIP_RETURN_CODE 77)

# Guards against CropWindow truncating from the origin instead of
# cropping -- rasterization must offset by the crop rectangle's own
# origin.
add_executable(gman_cropwindow cropwindow_test.cpp)
target_link_libraries(gman_cropwindow PRIVATE gman TIFF::TIFF)

set(GMAN_CROPWINDOW_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/cropwindow-run")
file(MAKE_DIRECTORY "${GMAN_CROPWINDOW_WORKDIR}")

add_test(NAME cropwindow
  COMMAND gman_cropwindow $<TARGET_FILE:gman_cli>
  WORKING_DIRECTORY "${GMAN_CROPWINDOW_WORKDIR}")
set_tests_properties(cropwindow PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Guards against orthographic ignoring RiScreenWindow and hard-clipping
# at camera-space x=+-1. GMANPolygonClipper::clip rebuilds its side
# planes from the screen window for both projections -- see the
# perspective analogue in gmanpolygonclipper.cpp.
add_executable(gman_orthowindow orthowindow_test.cpp)
target_link_libraries(gman_orthowindow PRIVATE gman TIFF::TIFF)

set(GMAN_ORTHOWINDOW_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/orthowindow-run")
file(MAKE_DIRECTORY "${GMAN_ORTHOWINDOW_WORKDIR}")

add_test(NAME orthowindow
  COMMAND gman_orthowindow $<TARGET_FILE:gman_cli>
  WORKING_DIRECTORY "${GMAN_ORTHOWINDOW_WORKDIR}")
set_tests_properties(orthowindow PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# Pins gmanrasterprimitivefactory.cpp's diceCountFor: n =
# clamp(ceil(L / sqrt(ShadingRate)), 1, 16), L an ear-clipped triangle's
# longest raster-space edge. See tests/polygondicecount_test.cpp.
add_executable(gman_polygondicecount polygondicecount_test.cpp)
target_link_libraries(gman_polygondicecount PRIVATE gman gman_raster gman_internal_headers)
add_test(NAME polygondicecount COMMAND gman_polygondicecount)
# getRSPolygonMesh falls back to dlopen'ing "libmatte.so" when no RiSurface
# is set -- triangulation's own comment, same lookup.
set_tests_properties(polygondicecount PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The fov warning fires once per RiWorldBegin regardless of polygon count: a
# two-Polygon perspective scene with no fov prints "FOV not set" exactly
# once. See tests/polygonfovwarning_test.cpp.
add_executable(gman_polygonfovwarning polygonfovwarning_test.cpp)
target_link_libraries(gman_polygonfovwarning PRIVATE gman)

set(GMAN_POLYGONFOVWARNING_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/polygonfovwarning-run")
file(MAKE_DIRECTORY "${GMAN_POLYGONFOVWARNING_WORKDIR}")

add_test(NAME polygonfovwarning
  COMMAND gman_polygonfovwarning $<TARGET_FILE:gman_cli>
                                 "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_POLYGONFOVWARNING_WORKDIR}")
set_tests_properties(polygonfovwarning PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# resolveViewingSystemInputs resolves "fov" through the same dictionary
# RiProjectionV's own parameter list used: a Declare'd "fov" prints no
# "FOV not set" and renders at the value the RIB set, not 90. See
# tests/declaredfov_test.cpp.
add_executable(gman_declaredfov declaredfov_test.cpp)
target_link_libraries(gman_declaredfov PRIVATE gman TIFF::TIFF)

set(GMAN_DECLAREDFOV_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/declaredfov-run")
file(MAKE_DIRECTORY "${GMAN_DECLAREDFOV_WORKDIR}")

add_test(NAME declaredfov
  COMMAND gman_declaredfov $<TARGET_FILE:gman_cli>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_DECLAREDFOV_WORKDIR}")
set_tests_properties(declaredfov PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# The actual gate: tests/rib/polygon.rib renders a convex pentagon whose
# raster coverage and shaded colour both match an analytically computed
# region and value.
add_executable(gman_polygon polygon_test.cpp)
target_link_libraries(gman_polygon PRIVATE gman gman_raster TIFF::TIFF gman_internal_headers)

set(GMAN_POLYGON_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/polygon-run")
file(MAKE_DIRECTORY "${GMAN_POLYGON_WORKDIR}")

add_test(NAME polygon
  COMMAND gman_polygon $<TARGET_FILE:gman_cli>
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
target_link_libraries(gman_samplebuffer PRIVATE gman TIFF::TIFF)

set(GMAN_SAMPLEBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/samplebuffer-run")
file(MAKE_DIRECTORY "${GMAN_SAMPLEBUFFER_WORKDIR}")

add_test(NAME samplebuffer
  COMMAND gman_samplebuffer $<TARGET_FILE:gman_cli>
  WORKING_DIRECTORY "${GMAN_SAMPLEBUFFER_WORKDIR}")
set_tests_properties(samplebuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# GMANSampleBuffer::resolve writes colour unclamped, alpha clamped to
# [0, 1]: a single above-1 sample passes through bit-equal, and a step
# edge under a negative-lobe kernel both undershoots and overshoots in
# colour while alpha stays in range. Drives GMANSampleBuffer and
# GMANFrameBuffer directly, no gman binary.
add_executable(gman_resolvehdr resolvehdr_test.cpp)
target_link_libraries(gman_resolvehdr PRIVATE gman)

add_test(NAME resolvehdr COMMAND gman_resolvehdr)
set_tests_properties(resolvehdr PROPERTIES
  LABELS unit
  TIMEOUT 30)

# A flat, fully-covered matte plane samples 1.6 -- above 1 -- so Exposure
# below 1 must recover it rather than dim a clipped 1. Both renderers
# write the same TIFF names, so each gets its own WORKING_DIRECTORY.
add_executable(gman_exposurerecovery exposurerecovery_test.cpp)
target_link_libraries(gman_exposurerecovery PRIVATE gman TIFF::TIFF)
add_dependencies(gman_exposurerecovery gman_zbuffer gman_matte)

set(GMAN_EXPOSURERECOVERY_ZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/exposurerecovery-zbuffer-run")
file(MAKE_DIRECTORY "${GMAN_EXPOSURERECOVERY_ZBUFFER_WORKDIR}")

add_test(NAME exposurerecovery_zbuffer
  COMMAND gman_exposurerecovery $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                gmanzbuffer
  WORKING_DIRECTORY "${GMAN_EXPOSURERECOVERY_ZBUFFER_WORKDIR}")
set_tests_properties(exposurerecovery_zbuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_exposurerecovery gman_raytracer)

  set(GMAN_EXPOSURERECOVERY_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/exposurerecovery-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_EXPOSURERECOVERY_RAYTRACER_WORKDIR}")

  add_test(NAME exposurerecovery_raytracer
    COMMAND gman_exposurerecovery $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                  gmanraytracer
    WORKING_DIRECTORY "${GMAN_EXPOSURERECOVERY_RAYTRACER_WORKDIR}")
  set_tests_properties(exposurerecovery_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# gman::sampleCentre: the raster centre of one sample, and the deleted
# (T, U, V) overload that keeps a non-int argument from truncating
# silently, pinned at compile time via a requires-expression.
add_executable(gman_samplecentre samplecentre_test.cpp)
target_link_libraries(gman_samplecentre PRIVATE gman)

add_test(NAME samplecentre COMMAND gman_samplecentre)
set_tests_properties(samplecentre PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Perspective clipping honours RiScreenWindow, not the canonical +-1 NDC
# cube: tests/rib/wideframe.rib's rectangle spans a wide, non-square
# frame's default aspect-derived window.
add_executable(gman_wideframe wideframe_test.cpp)
target_link_libraries(gman_wideframe PRIVATE gman TIFF::TIFF)

set(GMAN_WIDEFRAME_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/wideframe-run")
file(MAKE_DIRECTORY "${GMAN_WIDEFRAME_WORKDIR}")

add_test(NAME wideframe
  COMMAND gman_wideframe $<TARGET_FILE:gman_cli>
                         "${CMAKE_CURRENT_SOURCE_DIR}/rib/wideframe.rib"
  WORKING_DIRECTORY "${GMAN_WIDEFRAME_WORKDIR}")
set_tests_properties(wideframe PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# A wall crossing the frame edge shades as it does unclipped:
# tests/rib/clipframe_narrow.rib's left columns against the same pixels of
# tests/rib/clipframe_wide.rib, a frame whose own clip edge lies beyond them.
add_executable(gman_clipframeshading clipframeshading_test.cpp)
target_link_libraries(gman_clipframeshading PRIVATE gman TIFF::TIFF)

set(GMAN_CLIPFRAMESHADING_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/clipframeshading-run")
file(MAKE_DIRECTORY "${GMAN_CLIPFRAMESHADING_WORKDIR}")

add_test(NAME clipframeshading
  COMMAND gman_clipframeshading $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib/clipframe_narrow.rib"
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib/clipframe_wide.rib"
  WORKING_DIRECTORY "${GMAN_CLIPFRAMESHADING_WORKDIR}")
set_tests_properties(clipframeshading PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# The texture cache (gmantexture.h), the texture() shadeop and
# gman::makeTexture: direct sampler and writer assertions, tests/rib/texture.rib
# (a paintedplastic Patch read back for its four quadrants' colours) and
# tests/rib/maketexture_wrap.rib (the same, through a MakeTexture request).
add_executable(gman_texture texture_test.cpp)
target_link_libraries(gman_texture PRIVATE gman TIFF::TIFF gman_internal_headers)

set(GMAN_TEXTURE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/texture-run")
file(MAKE_DIRECTORY "${GMAN_TEXTURE_WORKDIR}")

add_test(NAME texture
  COMMAND gman_texture $<TARGET_FILE:gman_cli>
                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_TEXTURE_WORKDIR}")
set_tests_properties(texture PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# The texture cache as an argument: two caches decoding one file
# independently, a hand-built GMANSurfaceEnv and gman::shade routing
# through the cache each names (gman::textureCache() when they name none),
# and one cache per gman::parallelFor worker with no cross-worker hit. See
# tests/texturecache_test.cpp.
add_executable(gman_texturecache texturecache_test.cpp)
target_link_libraries(gman_texturecache PRIVATE gman TIFF::TIFF gman_internal_headers)
add_dependencies(gman_texturecache gman_paintedplastic)

set(GMAN_TEXTURECACHE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/texturecache-run")
file(MAKE_DIRECTORY "${GMAN_TEXTURECACHE_WORKDIR}")

add_test(NAME texturecache
  COMMAND gman_texturecache
  WORKING_DIRECTORY "${GMAN_TEXTURECACHE_WORKDIR}")
set_tests_properties(texturecache PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# A texture lookup at a non-finite s or t answers NaN, and at a finite
# coordinate beyond RtInt's range the texel its wrap mode defines. See
# tests/texturenonfinite_test.cpp.
add_executable(gman_texturenonfinite texturenonfinite_test.cpp)
target_link_libraries(gman_texturenonfinite PRIVATE gman TIFF::TIFF gman_internal_headers)

set(GMAN_TEXTURENONFINITE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/texturenonfinite-run")
file(MAKE_DIRECTORY "${GMAN_TEXTURENONFINITE_WORKDIR}")

add_test(NAME texturenonfinite COMMAND gman_texturenonfinite
  WORKING_DIRECTORY "${GMAN_TEXTURENONFINITE_WORKDIR}")
set_tests_properties(texturenonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# RiPixelFilter ignores a NaN or infinite width, and GMANSampleBuffer::resolve
# stays defined at any width. See tests/pixelfilternonfinite_test.cpp.
add_executable(gman_pixelfilternonfinite pixelfilternonfinite_test.cpp)
target_link_libraries(gman_pixelfilternonfinite PRIVATE gman gman_internal_headers)

add_test(NAME pixelfilternonfinite COMMAND gman_pixelfilternonfinite)
set_tests_properties(pixelfilternonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# RiFrameAspectRatio ignores a NaN or infinite ratio, getRasterInfo converts
# every size and crop bound defined, and the sample buffer refuses a sample
# extent past int's range. See tests/frameaspectnonfinite_test.cpp.
add_executable(gman_frameaspectnonfinite frameaspectnonfinite_test.cpp)
target_link_libraries(gman_frameaspectnonfinite PRIVATE gman gman_internal_headers)

add_test(NAME frameaspectnonfinite COMMAND gman_frameaspectnonfinite)
set_tests_properties(frameaspectnonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANOptions::setPixelSamples stores a rounded count in [1, 16] and a NaN as
# 1, whether the count comes through RiPixelSamples or straight into the
# setter. See tests/pixelsamplesnonfinite_test.cpp.
add_executable(gman_pixelsamplesnonfinite pixelsamplesnonfinite_test.cpp)
target_link_libraries(gman_pixelsamplesnonfinite PRIVATE gman gman_internal_headers)

add_test(NAME pixelsamplesnonfinite COMMAND gman_pixelsamplesnonfinite)
set_tests_properties(pixelsamplesnonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# gman::shade and gman::albedo read a NaN or infinite channel of a shader's
# answer as 0. See tests/shadingnonfinite_test.cpp.
add_executable(gman_shadingnonfinite shadingnonfinite_test.cpp)
target_link_libraries(gman_shadingnonfinite PRIVATE gman gman_internal_headers)

add_test(NAME shadingnonfinite COMMAND gman_shadingnonfinite)
set_tests_properties(shadingnonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# gman::runImager hands the imager a Ci with every channel finite. See
# tests/imagernonfinite_test.cpp.
add_executable(gman_imagernonfinite imagernonfinite_test.cpp)
target_link_libraries(gman_imagernonfinite PRIVATE gman gman_internal_headers)

add_test(NAME imagernonfinite COMMAND gman_imagernonfinite)
set_tests_properties(imagernonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANSampleBuffer resolves a NaN coverage to 1 and passes a finite coverage
# clamped to [0, 1] as before. See tests/samplebuffernonfinite_test.cpp.
add_executable(gman_samplebuffernonfinite samplebuffernonfinite_test.cpp)
target_link_libraries(gman_samplebuffernonfinite PRIVATE gman gman_internal_headers)

add_test(NAME samplebuffernonfinite COMMAND gman_samplebuffernonfinite)
set_tests_properties(samplebuffernonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# RiFormat ignores a NaN or infinite pixel aspect ratio. See
# tests/formatnonfinite_test.cpp.
add_executable(gman_formatnonfinite formatnonfinite_test.cpp)
target_link_libraries(gman_formatnonfinite PRIVATE gman gman_internal_headers)

add_test(NAME formatnonfinite COMMAND gman_formatnonfinite)
set_tests_properties(formatnonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# RiExposure ignores a NaN or infinite gain or gamma. See
# tests/exposurenonfinite_test.cpp.
add_executable(gman_exposurenonfinite exposurenonfinite_test.cpp)
target_link_libraries(gman_exposurenonfinite PRIVATE gman gman_internal_headers)

add_test(NAME exposurenonfinite COMMAND gman_exposurenonfinite)
set_tests_properties(exposurenonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANRadiosityMesh converts a polygon's extent over the maximum edge length
# into a cell count, and a hit's position into a cell, defined at any size. See
# tests/radiositynonfinite_test.cpp.
add_executable(gman_radiositynonfinite radiositynonfinite_test.cpp)
target_link_libraries(gman_radiositynonfinite PRIVATE gman_radiosity_objects)

add_test(NAME radiositynonfinite COMMAND gman_radiositynonfinite)
set_tests_properties(radiositynonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The colour an imager returns replaces the pixel, a NaN or infinite channel
# read as 0. See tests/imageroutputnonfinite_test.cpp.
add_executable(gman_imageroutputnonfinite imageroutputnonfinite_test.cpp)
target_link_libraries(gman_imageroutputnonfinite PRIVATE gman gman_internal_headers)

add_test(NAME imageroutputnonfinite COMMAND gman_imageroutputnonfinite)
set_tests_properties(imageroutputnonfinite PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# RiRotate about a zero axis leaves a transformation of NaN entries. Pins the
# wanted behavior; WILL_FAIL until RiRotate refuses a zero axis or answers a
# finite matrix for one. See tests/rotatezeroaxis_pin_test.cpp.
add_executable(gman_rotatezeroaxis_pin rotatezeroaxis_pin_test.cpp)
target_link_libraries(gman_rotatezeroaxis_pin PRIVATE gman gman_internal_headers)

add_test(NAME rotatezeroaxis_pin COMMAND gman_rotatezeroaxis_pin)
set_tests_properties(rotatezeroaxis_pin PROPERTIES
  WILL_FAIL TRUE
  LABELS unit
  TIMEOUT 30)

# Test-only surface shader answering a NaN or infinite Ci or Oi on request
# ("string ci", "string oi"), or an opacity whose interpolation overflows.
gman_add_plugin(gman_nonfinitesurface nonfinitesurface_test_plugin.cpp)
set_target_properties(gman_nonfinitesurface PROPERTIES OUTPUT_NAME "nonfinitesurface")

# One rule for coverage in every renderer: a surface whose Oi is NaN or +inf is
# opaque, and a NaN or infinite Ci shades black. See
# tests/coveragenonfinite_test.cpp.
add_executable(gman_coveragenonfinite coveragenonfinite_test.cpp)
target_link_libraries(gman_coveragenonfinite PRIVATE TIFF::TIFF)
add_dependencies(gman_coveragenonfinite gman_nonfinitesurface gman_zbuffer)

set(GMAN_COVERAGENONFINITE_ZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/coveragenonfinite-zbuffer-run")
file(MAKE_DIRECTORY "${GMAN_COVERAGENONFINITE_ZBUFFER_WORKDIR}")

add_test(NAME coveragenonfinite_zbuffer
  COMMAND gman_coveragenonfinite $<TARGET_FILE:gman_cli> gmanzbuffer
  WORKING_DIRECTORY "${GMAN_COVERAGENONFINITE_ZBUFFER_WORKDIR}")
set_tests_properties(coveragenonfinite_zbuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 60)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_coveragenonfinite gman_raytracer)

  set(GMAN_COVERAGENONFINITE_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/coveragenonfinite-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_COVERAGENONFINITE_RAYTRACER_WORKDIR}")

  add_test(NAME coveragenonfinite_raytracer
    COMMAND gman_coveragenonfinite $<TARGET_FILE:gman_cli> gmanraytracer
    WORKING_DIRECTORY "${GMAN_COVERAGENONFINITE_RAYTRACER_WORKDIR}")
  set_tests_properties(coveragenonfinite_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 60)
endif()

if(GMAN_BUILD_PATHTRACER)
  add_dependencies(gman_coveragenonfinite gman_pathtracer)

  set(GMAN_COVERAGENONFINITE_PATHTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/coveragenonfinite-pathtracer-run")
  file(MAKE_DIRECTORY "${GMAN_COVERAGENONFINITE_PATHTRACER_WORKDIR}")

  add_test(NAME coveragenonfinite_pathtracer
    COMMAND gman_coveragenonfinite $<TARGET_FILE:gman_cli> gmanpathtracer
    WORKING_DIRECTORY "${GMAN_COVERAGENONFINITE_PATHTRACER_WORKDIR}")
  set_tests_properties(coveragenonfinite_pathtracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 60)
endif()

# A missing texture warns once per world block under every renderer; libtiff stays silent.
add_executable(gman_texturewarningonce texturewarningonce_test.cpp)
target_link_libraries(gman_texturewarningonce PRIVATE gman)
add_dependencies(gman_texturewarningonce gman_zbuffer gman_paintedplastic)

set(GMAN_TEXTUREWARNINGONCE_ZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/texturewarningonce-zbuffer-run")
file(MAKE_DIRECTORY "${GMAN_TEXTUREWARNINGONCE_ZBUFFER_WORKDIR}")

add_test(NAME texturewarningonce_zbuffer
  COMMAND gman_texturewarningonce $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                  gmanzbuffer
  WORKING_DIRECTORY "${GMAN_TEXTUREWARNINGONCE_ZBUFFER_WORKDIR}")
set_tests_properties(texturewarningonce_zbuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_texturewarningonce gman_raytracer)

  set(GMAN_TEXTUREWARNINGONCE_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/texturewarningonce-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_TEXTUREWARNINGONCE_RAYTRACER_WORKDIR}")

  add_test(NAME texturewarningonce_raytracer
    COMMAND gman_texturewarningonce $<TARGET_FILE:gman_cli>
                                    "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                    gmanraytracer
    WORKING_DIRECTORY "${GMAN_TEXTUREWARNINGONCE_RAYTRACER_WORKDIR}")
  set_tests_properties(texturewarningonce_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

if(GMAN_BUILD_PATHTRACER)
  add_dependencies(gman_texturewarningonce gman_pathtracer)

  set(GMAN_TEXTUREWARNINGONCE_PATHTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/texturewarningonce-pathtracer-run")
  file(MAKE_DIRECTORY "${GMAN_TEXTUREWARNINGONCE_PATHTRACER_WORKDIR}")

  add_test(NAME texturewarningonce_pathtracer
    COMMAND gman_texturewarningonce $<TARGET_FILE:gman_cli>
                                    "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                    gmanpathtracer
    WORKING_DIRECTORY "${GMAN_TEXTUREWARNINGONCE_PATHTRACER_WORKDIR}")
  set_tests_properties(texturewarningonce_pathtracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# A ray renderer draws Patch and PatchMesh with the z-buffer's coverage, and
# the ray tracer with its flat colour where ambientlight alone lights the
# fixture. See tests/patchraycoverage_test.cpp.
add_executable(gman_patchraycoverage patchraycoverage_test.cpp)
target_link_libraries(gman_patchraycoverage PRIVATE gman TIFF::TIFF)
add_dependencies(gman_patchraycoverage gman_zbuffer gman_matte gman_plastic)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_patchraycoverage gman_raytracer)

  set(GMAN_PATCHRAYCOVERAGE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/patchraycoverage-run")
  file(MAKE_DIRECTORY "${GMAN_PATCHRAYCOVERAGE_WORKDIR}")

  add_test(NAME patchraycoverage
    COMMAND gman_patchraycoverage $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                  gmanraytracer
                                  patch_norender
                                  patch_bicubic
                                  patch_bilinear_twin
                                  patchmesh_bilinear
                                  patchmesh_bilinear_periodic
                                  patchmesh_bicubic
                                  patch_bicubic_curved
                                  patchmesh_bspline_tube
    WORKING_DIRECTORY "${GMAN_PATCHRAYCOVERAGE_WORKDIR}")
  set_tests_properties(patchraycoverage PROPERTIES
    ENVIRONMENT "${GMAN_PHASE2_ENV}"
    LABELS render
    TIMEOUT 120)
endif()

if(GMAN_BUILD_PATHTRACER)
  add_dependencies(gman_patchraycoverage gman_pathtracer)

  set(GMAN_PATCHPATHCOVERAGE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/patchpathcoverage-run")
  file(MAKE_DIRECTORY "${GMAN_PATCHPATHCOVERAGE_WORKDIR}")

  add_test(NAME patchpathcoverage
    COMMAND gman_patchraycoverage $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                  gmanpathtracer
                                  patch_bicubic
                                  patch_bicubic_curved
                                  patchmesh_bspline_tube
    WORKING_DIRECTORY "${GMAN_PATCHPATHCOVERAGE_WORKDIR}")
  set_tests_properties(patchpathcoverage PROPERTIES
    ENVIRONMENT "${GMAN_PHASE2_ENV}"
    LABELS render
    TIMEOUT 120)
endif()

# 16-bit TIFF textures at full depth: RGB, greyscale and associated-alpha
# files sampled at v / 65535, a 16-bit file the reader can take only at 8 bits
# warning once, 8-bit files sampling as before, and makeTexture and
# makeLatLongEnvironment writing the picture's own depth. See
# tests/texture16_test.cpp.
add_executable(gman_texture16 texture16_test.cpp)
target_link_libraries(gman_texture16 PRIVATE gman TIFF::TIFF gman_internal_headers)

set(GMAN_TEXTURE16_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/texture16-run")
file(MAKE_DIRECTORY "${GMAN_TEXTURE16_WORKDIR}")

add_test(NAME texture16
  COMMAND gman_texture16
  WORKING_DIRECTORY "${GMAN_TEXTURE16_WORKDIR}")
set_tests_properties(texture16 PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# paintedplastic's opacityname cuts a card out; the shadows of both renderers follow it.
add_executable(gman_textureopacity textureopacity_test.cpp)
target_link_libraries(gman_textureopacity PRIVATE gman TIFF::TIFF)
add_dependencies(gman_textureopacity gman_paintedplastic gman_matte)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_textureopacity gman_raytracer)

  set(GMAN_TEXTUREOPACITY_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/textureopacity-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_TEXTUREOPACITY_RAYTRACER_WORKDIR}")

  add_test(NAME textureopacity_raytracer
    COMMAND gman_textureopacity $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                gmanraytracer
    WORKING_DIRECTORY "${GMAN_TEXTUREOPACITY_RAYTRACER_WORKDIR}")
  set_tests_properties(textureopacity_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

if(GMAN_BUILD_PATHTRACER)
  add_dependencies(gman_textureopacity gman_pathtracer)

  set(GMAN_TEXTUREOPACITY_PATHTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/textureopacity-pathtracer-run")
  file(MAKE_DIRECTORY "${GMAN_TEXTUREOPACITY_PATHTRACER_WORKDIR}")

  add_test(NAME textureopacity_pathtracer
    COMMAND gman_textureopacity $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                gmanpathtracer
    WORKING_DIRECTORY "${GMAN_TEXTUREOPACITY_PATHTRACER_WORKDIR}")
  set_tests_properties(textureopacity_pathtracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 120)
endif()

# The ray tracer's shadows pass a clear glass slab by its dielectric's thin
# transmittance and a cut-out card by its opacity map, reading within a fixed
# tolerance of the path tracer's and at the predicted ratio of the unshadowed
# receiver. Needs both renderers. See tests/rayshadowtransmittance_test.cpp.
if(GMAN_BUILD_RAYTRACER AND GMAN_BUILD_PATHTRACER)
  add_executable(gman_rayshadowtransmittance rayshadowtransmittance_test.cpp)
  target_link_libraries(gman_rayshadowtransmittance PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_rayshadowtransmittance gman_raytracer gman_pathtracer gman_glass gman_matte
                   gman_paintedplastic)

  set(GMAN_RAYSHADOWTRANSMITTANCE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/rayshadowtransmittance-run")
  file(MAKE_DIRECTORY "${GMAN_RAYSHADOWTRANSMITTANCE_WORKDIR}")

  add_test(NAME rayshadowtransmittance
    COMMAND gman_rayshadowtransmittance $<TARGET_FILE:gman_cli>
                                        "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_RAYSHADOWTRANSMITTANCE_WORKDIR}")
  set_tests_properties(rayshadowtransmittance PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 300)
endif()

# The ray tracer lights a matte wall from an area emitter as the path tracer
# does: on four direct-only fixtures (a sphere, a disk, a square polygon and
# the polygon behind a glass slab) its lit and penumbra block means agree with
# the path tracer's within a tolerance of four standard errors plus a bias
# budget. Copies of the sphere fixture prove the draw-count option and the
# warning for an emitter the ray tracer skips. Needs both renderers. See
# tests/rayarealight_test.cpp.
if(GMAN_BUILD_RAYTRACER AND GMAN_BUILD_PATHTRACER)
  add_executable(gman_rayarealight rayarealight_test.cpp)
  target_link_libraries(gman_rayarealight PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_rayarealight gman_raytracer gman_pathtracer gman_matte gman_glass)

  set(GMAN_RAYAREALIGHT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/rayarealight-run")
  file(MAKE_DIRECTORY "${GMAN_RAYAREALIGHT_WORKDIR}")

  add_test(NAME rayarealight
    COMMAND gman_rayarealight $<TARGET_FILE:gman_cli>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_RAYAREALIGHT_WORKDIR}")
  set_tests_properties(rayarealight PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 300)
endif()

# The ray tracer's area-light noise near an emitter and far from it: on a
# matte sphere 0.3 above a large panel, disk or sphere the relative rms of a
# 16-draw render against a 1,024-draw one stays within 0.12, and on the panel,
# disk and sphere fixtures without their blocker that of a 4-draw render within
# 0.02. See tests/arealightnoise_test.cpp.
if(GMAN_BUILD_RAYTRACER)
  add_executable(gman_arealightnoise arealightnoise_test.cpp)
  target_link_libraries(gman_arealightnoise PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_arealightnoise gman_raytracer gman_matte)

  set(GMAN_AREALIGHTNOISE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/arealightnoise-run")
  file(MAKE_DIRECTORY "${GMAN_AREALIGHTNOISE_WORKDIR}")

  add_test(NAME arealightnoise
    COMMAND gman_arealightnoise $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_AREALIGHTNOISE_WORKDIR}")
  set_tests_properties(arealightnoise PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 120)
endif()

# The ray tracer composites what lies behind a partly opaque surface that a
# glass or mirror ray reaches: on a panel of Opacity 0.5 seen through a glass
# slab and in a mirror, the ray tracer's block means agree with the path
# tracer's within 1.5% and read half of the bare twins'; an ambient-lit scene,
# under the ray tracer alone, reads its mirrored block at Kr times its direct
# block. Needs both renderers. See tests/raycomposite_test.cpp.
if(GMAN_BUILD_RAYTRACER AND GMAN_BUILD_PATHTRACER)
  add_executable(gman_raycomposite raycomposite_test.cpp)
  target_link_libraries(gman_raycomposite PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_raycomposite gman_raytracer gman_pathtracer gman_matte gman_glass gman_mirror)

  set(GMAN_RAYCOMPOSITE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/raycomposite-run")
  file(MAKE_DIRECTORY "${GMAN_RAYCOMPOSITE_WORKDIR}")

  add_test(NAME raycomposite
    COMMAND gman_raycomposite $<TARGET_FILE:gman_cli>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_RAYCOMPOSITE_WORKDIR}")
  set_tests_properties(raycomposite PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 300)
endif()

# A smooth fold's terminator renders lit under both renderers, while every other blocker still shadows.
add_executable(gman_creasestreak creasestreak_test.cpp)
target_link_libraries(gman_creasestreak PRIVATE gman TIFF::TIFF)
add_dependencies(gman_creasestreak gman_matte)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_creasestreak gman_raytracer)

  set(GMAN_CREASESTREAK_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/creasestreak-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_CREASESTREAK_RAYTRACER_WORKDIR}")

  add_test(NAME creasestreak_raytracer
    COMMAND gman_creasestreak $<TARGET_FILE:gman_cli>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                              gmanraytracer
    WORKING_DIRECTORY "${GMAN_CREASESTREAK_RAYTRACER_WORKDIR}")
  set_tests_properties(creasestreak_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

if(GMAN_BUILD_PATHTRACER)
  add_dependencies(gman_creasestreak gman_pathtracer)

  set(GMAN_CREASESTREAK_PATHTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/creasestreak-pathtracer-run")
  file(MAKE_DIRECTORY "${GMAN_CREASESTREAK_PATHTRACER_WORKDIR}")

  add_test(NAME creasestreak_pathtracer
    COMMAND gman_creasestreak $<TARGET_FILE:gman_cli>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                              gmanpathtracer
    WORKING_DIRECTORY "${GMAN_CREASESTREAK_PATHTRACER_WORKDIR}")
  set_tests_properties(creasestreak_pathtracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 120)
endif()

# A smooth prism's terminator renders lit, its back side stays lit and a wall still shadows it.
add_executable(gman_shadowterminator shadowterminator_test.cpp)
target_link_libraries(gman_shadowterminator PRIVATE gman TIFF::TIFF)
add_dependencies(gman_shadowterminator gman_matte)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_shadowterminator gman_raytracer)

  set(GMAN_SHADOWTERMINATOR_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/shadowterminator-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_SHADOWTERMINATOR_RAYTRACER_WORKDIR}")

  add_test(NAME shadowterminator_raytracer
    COMMAND gman_shadowterminator $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                  gmanraytracer
    WORKING_DIRECTORY "${GMAN_SHADOWTERMINATOR_RAYTRACER_WORKDIR}")
  set_tests_properties(shadowterminator_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

if(GMAN_BUILD_PATHTRACER)
  add_dependencies(gman_shadowterminator gman_pathtracer)

  set(GMAN_SHADOWTERMINATOR_PATHTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/shadowterminator-pathtracer-run")
  file(MAKE_DIRECTORY "${GMAN_SHADOWTERMINATOR_PATHTRACER_WORKDIR}")

  add_test(NAME shadowterminator_pathtracer
    COMMAND gman_shadowterminator $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                  gmanpathtracer
    WORKING_DIRECTORY "${GMAN_SHADOWTERMINATOR_PATHTRACER_WORKDIR}")
  set_tests_properties(shadowterminator_pathtracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 120)
endif()

# A polygon's per-vertex "N" shades it smoothly, wound either way, under the z-buffer and the ray tracer, and a hole's vertices take their own under the z-buffer.
add_executable(gman_zbuffervertexnormals zbuffervertexnormals_test.cpp)
target_link_libraries(gman_zbuffervertexnormals PRIVATE gman TIFF::TIFF)
add_dependencies(gman_zbuffervertexnormals gman_matte gman_zbuffer)

set(GMAN_ZBUFFERVERTEXNORMALS_ZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/zbuffervertexnormals-zbuffer-run")
file(MAKE_DIRECTORY "${GMAN_ZBUFFERVERTEXNORMALS_ZBUFFER_WORKDIR}")

add_test(NAME zbuffervertexnormals_zbuffer
  COMMAND gman_zbuffervertexnormals $<TARGET_FILE:gman_cli>
                                    "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                    gmanzbuffer
  WORKING_DIRECTORY "${GMAN_ZBUFFERVERTEXNORMALS_ZBUFFER_WORKDIR}")
set_tests_properties(zbuffervertexnormals_zbuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_zbuffervertexnormals gman_raytracer)

  set(GMAN_ZBUFFERVERTEXNORMALS_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/zbuffervertexnormals-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_ZBUFFERVERTEXNORMALS_RAYTRACER_WORKDIR}")

  add_test(NAME zbuffervertexnormals_raytracer
    COMMAND gman_zbuffervertexnormals $<TARGET_FILE:gman_cli>
                                      "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                      gmanraytracer
    WORKING_DIRECTORY "${GMAN_ZBUFFERVERTEXNORMALS_RAYTRACER_WORKDIR}")
  set_tests_properties(zbuffervertexnormals_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# A normal map's light fades to zero at the smooth terminator under both renderers, and the lit face keeps its light.
add_executable(gman_bumpterminator bumpterminator_test.cpp)
target_link_libraries(gman_bumpterminator PRIVATE gman TIFF::TIFF)
add_dependencies(gman_bumpterminator gman_paintedplastic gman_matte)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_bumpterminator gman_raytracer)

  set(GMAN_BUMPTERMINATOR_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/bumpterminator-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_BUMPTERMINATOR_RAYTRACER_WORKDIR}")

  add_test(NAME bumpterminator_raytracer
    COMMAND gman_bumpterminator $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                gmanraytracer
    WORKING_DIRECTORY "${GMAN_BUMPTERMINATOR_RAYTRACER_WORKDIR}")
  set_tests_properties(bumpterminator_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

if(GMAN_BUILD_PATHTRACER)
  add_dependencies(gman_bumpterminator gman_pathtracer)

  set(GMAN_BUMPTERMINATOR_PATHTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/bumpterminator-pathtracer-run")
  file(MAKE_DIRECTORY "${GMAN_BUMPTERMINATOR_PATHTRACER_WORKDIR}")

  add_test(NAME bumpterminator_pathtracer
    COMMAND gman_bumpterminator $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                gmanpathtracer
    WORKING_DIRECTORY "${GMAN_BUMPTERMINATOR_PATHTRACER_WORKDIR}")
  set_tests_properties(bumpterminator_pathtracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 120)
endif()

# s, t (and, for a polygon, u, v) reaching texture() on quadrics, Patch and
# (General)Polygon: RiTextureCoordinates and the "s"/"t"/"st" primvars, over
# tests/rib/texcoords/*.rib -- each fixture copies tests/rib/texture.rib's
# own frame and checker.
add_executable(gman_texcoords texcoords_test.cpp)
target_link_libraries(gman_texcoords PRIVATE gman TIFF::TIFF)

set(GMAN_TEXCOORDS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/texcoords-run")
file(MAKE_DIRECTORY "${GMAN_TEXCOORDS_WORKDIR}")

add_test(NAME texcoords
  COMMAND gman_texcoords $<TARGET_FILE:gman_cli>
                         "${CMAKE_CURRENT_SOURCE_DIR}/rib/texcoords"
  WORKING_DIRECTORY "${GMAN_TEXCOORDS_WORKDIR}")
set_tests_properties(texcoords PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

# Guards buildPolygonObject's own dicing resolution against a future,
# silent coarsening: a Polygon square, diced the ordinary way, must match a
# much finer hand-diced equivalent of the identical geometry and texture
# within checkGoldenImage's own tolerance. See tests/polygondicing_test.cpp.
add_executable(gman_polygondicing polygondicing_test.cpp)
target_link_libraries(gman_polygondicing PRIVATE gman TIFF::TIFF)

set(GMAN_POLYGONDICING_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/polygondicing-run")
file(MAKE_DIRECTORY "${GMAN_POLYGONDICING_WORKDIR}")

add_test(NAME polygondicing
  COMMAND gman_polygondicing $<TARGET_FILE:gman_cli>
  WORKING_DIRECTORY "${GMAN_POLYGONDICING_WORKDIR}")
set_tests_properties(polygondicing PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

# PointsPolygons and PointsGeneralPolygons: every face is the equivalent
# Polygon/GeneralPolygon, gathered through "verts" out of a shared "P".
# Direct object-manager calls, twin-RIB renders compared pixel by pixel
# (tests/goldenimage.h) and malformed request fixtures. See
# tests/pointspolygons_test.cpp.
add_executable(gman_pointspolygons pointspolygons_test.cpp)
target_link_libraries(gman_pointspolygons PRIVATE gman gman_raster TIFF::TIFF gman_internal_headers)

set(GMAN_POINTSPOLYGONS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/pointspolygons-run")
file(MAKE_DIRECTORY "${GMAN_POINTSPOLYGONS_WORKDIR}")

add_test(NAME pointspolygons
  COMMAND gman_pointspolygons $<TARGET_FILE:gman_cli>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_POINTSPOLYGONS_WORKDIR}")
set_tests_properties(pointspolygons PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

# NuPatch: GMANNuPatch implements GMANParametric with a Cox-de Boor
# evaluator, RiNuPatchV validates and sizes the request, parseNuPatch
# checks knot lengths and owns its arrays. Evaluator cases (direct
# construction), twin-RIB renders and texture-coordinates no-op fixtures
# compared pixel by pixel (tests/goldenimage.h), the ray tracer's one warning
# for a NuPatch it cannot render, and malformed/exception fixtures. See
# tests/nupatch_test.cpp.
add_executable(gman_nupatch nupatch_test.cpp)
target_link_libraries(gman_nupatch PRIVATE gman TIFF::TIFF)

set(GMAN_NUPATCH_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/nupatch-run")
file(MAKE_DIRECTORY "${GMAN_NUPATCH_WORKDIR}")

add_test(NAME nupatch
  COMMAND gman_nupatch $<TARGET_FILE:gman_cli>
                       "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_NUPATCH_WORKDIR}")
set_tests_properties(nupatch PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 40)

# Test-only surface shader (see its own comment): a dlopen'd module and its
# caller share nothing but computeCi's own return value, so
# environment_test.cpp's plumbing proof needs a real, loadable plugin to
# read GMANSurfaceEnv::cameraToWorld back out of a real shading call.
gman_add_plugin(gman_camerashader camerashader_test_plugin.cpp)
set_target_properties(gman_camerashader PROPERTIES OUTPUT_NAME "camerashader")

# Test-only volume shader (see its own comment): the shader
# unknownsurface_test.cpp's "loaded, but not a surface" fixture names, and
# shadermodulenames_test.cpp's Atmosphere/Interior/Exterior success check
# loads, so each type-check branch has a plugin to reach it with.
gman_add_plugin(gman_notasurface notasurface_test_plugin.cpp)
set_target_properties(gman_notasurface PROPERTIES OUTPUT_NAME "notasurface")

# Test-only surface shader (see its own comment): counts its own live
# instances, read back through its own exported GMANCountingShaderLiveCount,
# so shaderinstance_test.cpp can prove GMANLoadableShader's ownership from
# outside; shaderbsdf_test.cpp also loads it, as the plugin without a bsdf
# override its own loaded-default case needs.
gman_add_plugin(gman_countingshader countingshader_test_plugin.cpp)
set_target_properties(gman_countingshader PROPERTIES OUTPUT_NAME "countingshader")

# Test-only surface shader (see its own comment): defines GMANLoadShader but
# not GMANDestroyShader, standing in for a plugin lacking the destroy entry
# point.
gman_add_plugin(gman_nodestroyshader nodestroyshader_test_plugin.cpp)
set_target_properties(gman_nodestroyshader PROPERTIES OUTPUT_NAME "nodestroyshader")

# Test-only indirect-light pass (see its comment): answers black until
# prepare() has seen a primitive, kConstantIndirect after, and counts its
# live instances through its exported GMANConstantIndirectLiveCount.
gman_add_plugin(gman_constantindirect constantindirect_test_plugin.cpp)
target_link_libraries(gman_constantindirect PRIVATE gman_ray)
set_target_properties(gman_constantindirect PROPERTIES OUTPUT_NAME "constantindirect")

# Test-only indirect-light pass whose GMANCreateIndirectPass always
# returns null, pinning loadIndirectPass's warning on that failure.
gman_add_plugin(gman_nullindirect nullindirect_test_plugin.cpp)
target_link_libraries(gman_nullindirect PRIVATE gman_ray)
set_target_properties(gman_nullindirect PROPERTIES OUTPUT_NAME "nullindirect")

# One shader instance per Surface call: distinct instances, interleaved
# shading with no cross-talk, correct lifetime and destruction, and a
# plugin missing GMANDestroyShader refused. See tests/shaderinstance_test.cpp.
add_executable(gman_shaderinstance shaderinstance_test.cpp)
target_link_libraries(gman_shaderinstance PRIVATE gman)
add_dependencies(gman_shaderinstance gman_matte gman_plastic gman_metal gman_paintedplastic gman_shinymetal
  gman_mirror gman_glass gman_countingshader gman_nodestroyshader)

add_test(NAME shaderinstance COMMAND gman_shaderinstance)
set_tests_properties(shaderinstance PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Test-only displacement shader (see its own comment): a correctly typed,
# real, loadable displacement plugin, so shadermodulenames_test.cpp's
# Displacement success check has a plugin to load.
gman_add_plugin(gman_identitydisplacement identitydisplacement_test_plugin.cpp)
set_target_properties(gman_identitydisplacement PROPERTIES OUTPUT_NAME "identitydisplacement")

# Test-only imager shader (see its own comment): counts its own live
# instances, read back through its own exported GMANCountingImagerLiveCount,
# so imagerownership_test.cpp and shadermodulenames_test.cpp can prove
# GMANOptions::imagerModule's ownership from outside.
gman_add_plugin(gman_countingimager countingimager_test_plugin.cpp)
set_target_properties(gman_countingimager PROPERTIES OUTPUT_NAME "countingimager")

# Test-only volume shader (see its own comment): counts its own live
# instances, read back through its own exported GMANCountingVolumeLiveCount,
# so shadermodulenames_test.cpp can prove a failed setAtmosphere releases
# the module it replaces.
gman_add_plugin(gman_countingvolume countingvolume_test_plugin.cpp)
set_target_properties(gman_countingvolume PROPERTIES OUTPUT_NAME "countingvolume")

# Every shader kind resolves its token to lib<name>.so and falls back to a
# clean, unset state on failure. See tests/shadermodulenames_test.cpp.
add_executable(gman_shadermodulenames shadermodulenames_test.cpp)
target_link_libraries(gman_shadermodulenames PRIVATE gman)
add_dependencies(gman_shadermodulenames gman_zbuffer gman_matte gman_notasurface gman_nodestroyshader
  gman_identitydisplacement gman_plastic gman_countingimager gman_countingvolume)

add_test(NAME shadermodulenames COMMAND gman_shadermodulenames)
set_tests_properties(shadermodulenames PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANOptions::imagerModule frees exactly one instance per owner, driven
# through the real C API with a nested RiFrameBegin/RiImagerV/RiFrameEnd, so
# an instance lives until its last owning frame releases it. See
# tests/imagerownership_test.cpp.
add_executable(gman_imagerownership imagerownership_test.cpp)
target_link_libraries(gman_imagerownership PRIVATE gman)
add_dependencies(gman_imagerownership gman_countingimager gman_zbuffer)

add_test(NAME imagerownership COMMAND gman_imagerownership)
set_tests_properties(imagerownership PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Imager "background" places its colour behind each pixel's uncovered part under
# every renderer, after exposure and before quantization, and writes alpha 1.
# See imagerbackground_test.cpp's own comment. Each renderer writes the same
# TIFF names, so each gets its own WORKING_DIRECTORY.
add_executable(gman_imagerbackground imagerbackground_test.cpp)
target_link_libraries(gman_imagerbackground PRIVATE gman TIFF::TIFF)
add_dependencies(gman_imagerbackground gman_background gman_matte gman_zbuffer)

set(GMAN_IMAGERBACKGROUND_ZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/imagerbackground-zbuffer-run")
file(MAKE_DIRECTORY "${GMAN_IMAGERBACKGROUND_ZBUFFER_WORKDIR}")

add_test(NAME imagerbackground_zbuffer
  COMMAND gman_imagerbackground $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                gmanzbuffer
  WORKING_DIRECTORY "${GMAN_IMAGERBACKGROUND_ZBUFFER_WORKDIR}")
set_tests_properties(imagerbackground_zbuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_imagerbackground gman_raytracer)

  set(GMAN_IMAGERBACKGROUND_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/imagerbackground-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_IMAGERBACKGROUND_RAYTRACER_WORKDIR}")

  add_test(NAME imagerbackground_raytracer
    COMMAND gman_imagerbackground $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                  gmanraytracer
    WORKING_DIRECTORY "${GMAN_IMAGERBACKGROUND_RAYTRACER_WORKDIR}")
  set_tests_properties(imagerbackground_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

if(GMAN_BUILD_PATHTRACER)
  add_dependencies(gman_imagerbackground gman_pathtracer)

  set(GMAN_IMAGERBACKGROUND_PATHTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/imagerbackground-pathtracer-run")
  file(MAKE_DIRECTORY "${GMAN_IMAGERBACKGROUND_PATHTRACER_WORKDIR}")

  add_test(NAME imagerbackground_pathtracer
    COMMAND gman_imagerbackground $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                  gmanpathtracer
    WORKING_DIRECTORY "${GMAN_IMAGERBACKGROUND_PATHTRACER_WORKDIR}")
  set_tests_properties(imagerbackground_pathtracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# GMANSurfaceEnv::ambient() adds indirect when it is bound, directly and
# through gman::shade against a matte appearance loaded the same factory
# way. See tests/indirectambient_test.cpp.
add_executable(gman_indirectambient indirectambient_test.cpp)
target_link_libraries(gman_indirectambient PRIVATE gman)
add_dependencies(gman_indirectambient gman_matte)

add_test(NAME indirectambient COMMAND gman_indirectambient)
set_tests_properties(indirectambient PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# gman::hitSurfacePoint's field mapping off a real BVH hit, and
# gman::albedo's parity with gman::shade's Ci at the same point for a
# sphere and a polygon carrying "st". A null shader answers Cs clamped to
# [0, 1]. See tests/albedoenv_test.cpp.
add_executable(gman_albedoenv albedoenv_test.cpp)
target_link_libraries(gman_albedoenv PRIVATE gman gman_ray)

add_test(NAME albedoenv COMMAND gman_albedoenv)
set_tests_properties(albedoenv PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The shading-language spline family in include/gmanslapi.h and
# GMANSurfaceEnv::spline's basis dispatch: each basis evaluates its own curve,
# reads only its control values at and beyond the ends of [0, 1], and an
# unknown basis name throws, and a NaN value evaluates as value 0. See
# tests/slspline_test.cpp.
add_executable(gman_slspline slspline_test.cpp)
target_link_libraries(gman_slspline PRIVATE gman)

add_test(NAME slspline COMMAND gman_slspline)
set_tests_properties(slspline PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# RiOptionV's handling of Option "radiosity" "float elementsize":
# unset reads 0, a later Option replaces an earlier one, a non-finite or
# non-positive value leaves it unchanged, and an unrelated Option --
# "render" "string indirect" included -- changes nothing. See
# tests/radiosityoption_test.cpp.
add_executable(gman_radiosityoption radiosityoption_test.cpp)
target_link_libraries(gman_radiosityoption PRIVATE gman gman_internal_headers)

add_test(NAME radiosityoption COMMAND gman_radiosityoption)
set_tests_properties(radiosityoption PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# gman::loadIndirectPass: a real module answers through its entry
# points, a missing or wrong module answers null, a module whose create
# function itself returns null warns and answers null too, and the
# deleter frees through the module rather than through a plain delete.
# See tests/indirectpass_test.cpp.
add_executable(gman_indirectpass indirectpass_test.cpp)
target_link_libraries(gman_indirectpass PRIVATE gman gman_ray gman_internal_headers)
add_dependencies(gman_indirectpass gman_constantindirect gman_nullindirect gman_matte)

add_test(NAME indirectpass COMMAND gman_indirectpass)
set_tests_properties(indirectpass PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# environment(), MakeLatLongEnvironment and shinymetal (RISpec 3.2 Sec
# 7.1.2, Sec 15.7.2, Appendix A.2.4): camera-to-world reaching the shading
# path, the writer and lookup, and shinymetal rendered under three camera
# orientations. See tests/environment_test.cpp.
add_executable(gman_environment environment_test.cpp)
target_link_libraries(gman_environment PRIVATE gman gman_raster TIFF::TIFF gman_internal_headers)
add_dependencies(gman_environment gman_camerashader gman_shinymetal)

set(GMAN_ENVIRONMENT_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/environment-run")
file(MAKE_DIRECTORY "${GMAN_ENVIRONMENT_WORKDIR}")

add_test(NAME environment
  COMMAND gman_environment $<TARGET_FILE:gman_cli>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_ENVIRONMENT_WORKDIR}")
set_tests_properties(environment PROPERTIES
  ENVIRONMENT "${GMAN_PHASE2_ENV}"
  LABELS render
  TIMEOUT 30)

# gmanlog's sink: setLogFile releases logMutex before its own call to
# info(), so it does not deadlock. Also proves one trailing newline, a
# suppressed level skipping formatting, and eight concurrent writers landing
# 8000 whole lines.
add_executable(gman_log log_test.cpp)
target_link_libraries(gman_log PRIVATE gman)

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
target_link_libraries(gman_parallel PRIVATE gman gman_internal_headers)

add_test(NAME parallel COMMAND gman_parallel)
set_tests_properties(parallel PROPERTIES
  LABELS unit
  TIMEOUT 30)

# The progress line: the formatter, its durations and the option that asks for it.
# See tests/renderprogressformat_test.cpp.
add_executable(gman_renderprogressformat renderprogressformat_test.cpp)
target_link_libraries(gman_renderprogressformat PRIVATE gman gman_ray)

add_test(NAME renderprogressformat COMMAND gman_renderprogressformat)
set_tests_properties(renderprogressformat PROPERTIES
  LABELS unit
  TIMEOUT 30)

# RowProgress logs one line per whole percent, however many rows finish between two.
# See tests/renderprogressrows_test.cpp.
add_executable(gman_renderprogressrows renderprogressrows_test.cpp)
target_link_libraries(gman_renderprogressrows PRIVATE gman gman_ray)

add_test(NAME renderprogressrows COMMAND gman_renderprogressrows)
set_tests_properties(renderprogressrows PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Option "statistics" "integer progress" [1] makes the ray and path tracers log one
# line per whole percent of rows, ending at 100%, and leaves the image unchanged.
# See tests/renderprogress_test.cpp.
if(GMAN_BUILD_RAYTRACER AND GMAN_BUILD_PATHTRACER)
  add_executable(gman_renderprogress renderprogress_test.cpp)
  add_dependencies(gman_renderprogress gman_raytracer gman_pathtracer gman_matte)

  set(GMAN_RENDERPROGRESS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/renderprogress-run")
  file(MAKE_DIRECTORY "${GMAN_RENDERPROGRESS_WORKDIR}")

  add_test(NAME renderprogress
    COMMAND gman_renderprogress $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                gmanraytracer gmanpathtracer
    WORKING_DIRECTORY "${GMAN_RENDERPROGRESS_WORKDIR}")
  set_tests_properties(renderprogress PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 60)
endif()

# Library layering: every include in the production tree points down a layer,
# a private header stays in its library, and a file names only its library's
# export macro. See tests/librarylayering_test.cpp for the table.
add_executable(gman_librarylayering librarylayering_test.cpp)
add_test(NAME librarylayering
  COMMAND gman_librarylayering "${CMAKE_SOURCE_DIR}" ${GMAN_SOURCE_PATHS})
set_tests_properties(librarylayering PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::gammaCorrected: identity at gain 1/gamma 1 (no golden image moves), the
# pow(gain * channel, 1/gamma) shape, and the sub-byte precision case.
add_executable(gman_gamma gamma_test.cpp)
target_link_libraries(gman_gamma PRIVATE gman)

add_test(NAME gamma COMMAND gman_gamma)
set_tests_properties(gamma PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANSurfaceEnv's occlusion hook -- diffuse()/specular() scale each
# non-ambient light by occluder->transmission(), ambient() ignores
# it, and a point/distant light reports the distance and
# direction the contract promises.
add_executable(gman_occlusion occlusion_test.cpp)
target_link_libraries(gman_occlusion PRIVATE gman)

add_test(NAME occlusion COMMAND gman_occlusion)
set_tests_properties(occlusion PROPERTIES
  LABELS unit
  TIMEOUT 30)

# The tracer hook -- a null tracer degrades trace() to black, a bound
# one sees this env's own P/Ng and the caller's R, and gman::shade
# forwards its own tracer argument to the shader it runs.
add_executable(gman_trace trace_test.cpp)
target_link_libraries(gman_trace PRIVATE gman)

add_test(NAME trace COMMAND gman_trace)
set_tests_properties(trace PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANReflect, GMANRefract and GMANFresnel at known angles, through
# GMANSurfaceEnv's own wrapper and, for the six-argument overload, the
# free function directly.
add_executable(gman_reflectrefract reflectrefract_test.cpp)
target_link_libraries(gman_reflectrefract PRIVATE gman)

add_test(NAME reflectrefract COMMAND gman_reflectrefract)
set_tests_properties(reflectrefract PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Mirror and glass, loaded exactly as defaultSurfaceShader() loads
# matte -- libmirror.so/libglass.so built first, found by bare name at
# runtime through LD_LIBRARY_PATH/DYLD_LIBRARY_PATH.
add_executable(gman_mirrorglass mirrorglass_test.cpp)
target_link_libraries(gman_mirrorglass PRIVATE gman)
add_dependencies(gman_mirrorglass gman_mirror gman_glass)

add_test(NAME mirrorglass COMMAND gman_mirrorglass)
set_tests_properties(mirrorglass PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The diffuse-albedo query: the base default and each built-in shader's own
# answer, loaded the same way mirrorglass_test.cpp loads mirror and glass.
# Its own working directory, since the paintedplastic case writes and reads
# a texture file relative to cwd.
add_executable(gman_shaderalbedo shaderalbedo_test.cpp)
target_link_libraries(gman_shaderalbedo PRIVATE gman TIFF::TIFF)
add_dependencies(gman_shaderalbedo gman_matte gman_plastic gman_paintedplastic gman_metal gman_shinymetal gman_mirror
  gman_glass)

set(GMAN_SHADERALBEDO_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/shaderalbedo-run")
file(MAKE_DIRECTORY "${GMAN_SHADERALBEDO_WORKDIR}")

add_test(NAME shaderalbedo COMMAND gman_shaderalbedo
  WORKING_DIRECTORY "${GMAN_SHADERALBEDO_WORKDIR}")
set_tests_properties(shaderalbedo PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# paintedplastic's "normalname" bends its shading normal in computeCi and bsdf.
add_executable(gman_normalmap normalmap_test.cpp)
target_link_libraries(gman_normalmap PRIVATE gman TIFF::TIFF)
add_dependencies(gman_normalmap gman_paintedplastic)

set(GMAN_NORMALMAP_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/normalmap-run")
file(MAKE_DIRECTORY "${GMAN_NORMALMAP_WORKDIR}")

add_test(NAME normalmap COMMAND gman_normalmap
  WORKING_DIRECTORY "${GMAN_NORMALMAP_WORKDIR}")
set_tests_properties(normalmap PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Ray tracer core, unit 1: GMANViewingSystem answers a camera-space ray
# (cameraRay) as well as a world-space one (ray), and pins the [tmin,
# tmax] interval every GMANRay defaults to.
add_executable(gman_cameraray cameraray_test.cpp)
target_link_libraries(gman_cameraray PRIVATE gman gman_internal_headers)

add_test(NAME cameraray COMMAND gman_cameraray)
set_tests_properties(cameraray PROPERTIES
  LABELS unit
  TIMEOUT 30)

# solveRayQuadratic, the double a*t^2+b*t+c == 0 solver the
# sphere and the four swept quadrics' intersectors share, its linear
# branch exact and its quadratic branch stable under cancellation.
add_executable(gman_rayquadratic rayquadratic_test.cpp)
target_link_libraries(gman_rayquadratic PRIVATE gman gman_ray gman_ray_internal_headers)

add_test(NAME rayquadratic COMMAND gman_rayquadratic)
set_tests_properties(rayquadratic PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::solveQuartic against quartics built from known
# dyadic roots. The compiled solver lives in gman_ray; gmanrayquartic.h
# is a libgmanray/ header, reached through gman_ray_internal_headers.
add_executable(gman_rayquartic rayquartic_test.cpp)
target_link_libraries(gman_rayquartic PRIVATE gman gman_ray gman_ray_internal_headers)

add_test(NAME rayquartic COMMAND gman_rayquartic)
set_tests_properties(rayquartic PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Ray tracer core, unit 2: GMANRaySphere::intersect, linked against
# gman_ray, where ray geometry lives unconditionally, so this still
# builds and runs with GMAN_BUILD_RAYTRACER off.
add_executable(gman_raysphere raysphere_test.cpp)
target_link_libraries(gman_raysphere PRIVATE gman gman_ray)

add_test(NAME raysphere COMMAND gman_raysphere)
set_tests_properties(raysphere PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Every quadric intersector's hit sits on its implicit surface
# and its own ray at origin distances 5, 1e4 and 1e6, linked against
# gman_ray for the same reason as gman_raysphere above.
add_executable(gman_rayhitprecision rayhitprecision_test.cpp)
target_link_libraries(gman_rayhitprecision PRIVATE gman gman_ray)

add_test(NAME rayhitprecision COMMAND gman_rayhitprecision)
set_tests_properties(rayhitprecision PROPERTIES
  LABELS unit
  TIMEOUT 60)

# GMANRayDisk::intersect, linked against gman_ray for the
# same reason as gman_raysphere above.
add_executable(gman_raydisk raydisk_test.cpp)
target_link_libraries(gman_raydisk PRIVATE gman gman_ray)

add_test(NAME raydisk COMMAND gman_raydisk)
set_tests_properties(raydisk PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRayPolygon::intersect, linked against gman_ray for
# the same reason as gman_raysphere above.
add_executable(gman_raypolygon raypolygon_test.cpp)
target_link_libraries(gman_raypolygon PRIVATE gman gman_ray)

add_test(NAME raypolygon COMMAND gman_raypolygon)
set_tests_properties(raypolygon PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRayCylinder::intersect, linked against gman_ray for
# the same reason as gman_raysphere above.
add_executable(gman_raycylinder raycylinder_test.cpp)
target_link_libraries(gman_raycylinder PRIVATE gman gman_ray)

add_test(NAME raycylinder COMMAND gman_raycylinder)
set_tests_properties(raycylinder PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRayCone::intersect, linked against gman_ray for the
# same reason as gman_raysphere above.
add_executable(gman_raycone raycone_test.cpp)
target_link_libraries(gman_raycone PRIVATE gman gman_ray)

add_test(NAME raycone COMMAND gman_raycone)
set_tests_properties(raycone PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRayParaboloid::intersect, linked against gman_ray
# for the same reason as gman_raysphere above.
add_executable(gman_rayparaboloid rayparaboloid_test.cpp)
target_link_libraries(gman_rayparaboloid PRIVATE gman gman_ray)

add_test(NAME rayparaboloid COMMAND gman_rayparaboloid)
set_tests_properties(rayparaboloid PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRayHyperboloid::intersect, linked against gman_ray
# for the same reason as gman_raysphere above.
add_executable(gman_rayhyperboloid rayhyperboloid_test.cpp)
target_link_libraries(gman_rayhyperboloid PRIVATE gman gman_ray)

add_test(NAME rayhyperboloid COMMAND gman_rayhyperboloid)
set_tests_properties(rayhyperboloid PROPERTIES
  LABELS unit
  TIMEOUT 30)

# The flat-annulus hyperboloid (point1.z == point2.z): z carries no
# information about v on this plane, so v comes from the plane hit's own
# radius, and the wedge is a spiral sector since phi(v) varies with v.
add_executable(gman_hyperboloidflatannulus hyperboloidflatannulus_test.cpp)
target_link_libraries(gman_hyperboloidflatannulus PRIVATE gman gman_ray)

add_test(NAME hyperboloidflatannulus COMMAND gman_hyperboloidflatannulus)
set_tests_properties(hyperboloidflatannulus PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRayTorus::intersect, linked against
# gman_ray for the same reason as gman_raysphere above.
add_executable(gman_raytorus raytorus_test.cpp)
target_link_libraries(gman_raytorus PRIVATE gman gman_ray)

add_test(NAME raytorus COMMAND gman_raytorus)
set_tests_properties(raytorus PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Every ray primitive's camera-space bbox -- containment, tightness,
# the singular guard and the degenerate, non-singular case -- linked
# against gman_ray for the same reason as gman_raysphere above. See
# tests/raybbox_test.cpp.
add_executable(gman_raybbox raybbox_test.cpp)
target_link_libraries(gman_raybbox PRIVATE gman gman_ray)

add_test(NAME raybbox COMMAND gman_raybbox)
set_tests_properties(raybbox PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The seven quadrics' getObjectToCamera() and GMANRayPolygon's
# getOuterLoop()/getPlaneNormal()/isDegenerate(): each returns exactly what
# its primitive was built with, linked against gman_ray -- every
# header these accessors need is public. See
# tests/rayplacementaccessors_test.cpp.
add_executable(gman_rayplacementaccessors rayplacementaccessors_test.cpp)
target_link_libraries(gman_rayplacementaccessors PRIVATE gman gman_ray)

add_test(NAME rayplacementaccessors COMMAND gman_rayplacementaccessors)
set_tests_properties(rayplacementaccessors PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRayBVH, tested standalone against a hand-built
# GMANWorld -- identical hits against a linear scan and a
# bounded primitive-test count -- linked against gman_ray for the same
# reason as gman_raysphere above. See tests/raybvh_test.cpp.
add_executable(gman_raybvh raybvh_test.cpp)
target_link_libraries(gman_raybvh PRIVATE gman gman_ray)

add_test(NAME raybvh COMMAND gman_raybvh)
set_tests_properties(raybvh PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# No NaN enters, crosses or leaves the ray kernel: untraceable rays, a
# denormal direction component and a NaN box through GMANRayBVH. See
# tests/raykernelnan_test.cpp.
add_executable(gman_raykernelnan raykernelnan_test.cpp)
target_link_libraries(gman_raykernelnan PRIVATE gman gman_ray)

add_test(NAME raykernelnan COMMAND gman_raykernelnan)
set_tests_properties(raykernelnan PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANHit's dPdu and dPdv for every ray primitive, by finite differences on
# the intersector's own hits, and through gman::shade to a shader. See
# tests/raysurfacederivatives_test.cpp.
add_executable(gman_raysurfacederivatives raysurfacederivatives_test.cpp)
target_link_libraries(gman_raysurfacederivatives PRIVATE gman gman_ray)

add_test(NAME raysurfacederivatives COMMAND gman_raysurfacederivatives)
set_tests_properties(raysurfacederivatives PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Proves GMANRayBVH::nearestHit and GMANRayOccluder::transmission
# re-entrant: a probe primitive nests a second call, on the same tree, mid-
# traversal, and both the nested and the outer result must match their own
# standalone calls. Linked against gman_ray, where both live --
# the proof that the geometry, the BVH and the occluder are gman_ray's, not
# the plugin's.
add_executable(gman_raybvhreentrant raybvhreentrant_test.cpp)
target_link_libraries(gman_raybvhreentrant PRIVATE gman gman_ray)

add_test(NAME raybvhreentrant COMMAND gman_raybvhreentrant)
set_tests_properties(raybvhreentrant PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRayBVH::nearestHit's own traversal, and GMANRayOccluder::transmission's
# walk across it, make no heap allocation -- a global operator new/delete
# override counts allocations across a batch of calls. The override lives in
# raybvhallocator.cpp, its own translation unit, so a compiler inlining a
# call site back to malloc/free sees only one half of a pairing in each
# file. See tests/raybvhalloc_test.cpp.
add_executable(gman_raybvhalloc raybvhalloc_test.cpp raybvhallocator.cpp)
target_link_libraries(gman_raybvhalloc PRIVATE gman gman_ray)

add_test(NAME raybvhalloc COMMAND gman_raybvhalloc)
set_tests_properties(raybvhalloc PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRayPrimitiveFactory::getRSPolygonMesh builds a multi-loop GMANRayPolygon
# for a GeneralPolygon face -- a hole misses at its centre, the ring
# between hole and edge hits, and a point outside the outer loop misses --
# and a Polygon's one-loop case keeps the same hits on the same three rays.
# See tests/raypolygonhole_test.cpp.
add_executable(gman_raypolygonhole raypolygonhole_test.cpp)
target_link_libraries(gman_raypolygonhole PRIVATE gman gman_ray gman_internal_headers)

add_test(NAME raypolygonhole COMMAND gman_raypolygonhole)
set_tests_properties(raypolygonhole PROPERTIES
  LABELS unit
  TIMEOUT 30)

# A ray through the boundary of a polygon's outer ring hits it, so a ceiling
# and two walls meeting at shared edges leave no gap, on any side of the
# ring's projected axes; a ray 1e-3 past an edge still misses, and the
# closure's tolerance sits between 1e-5 and 5e-5 past. See
# tests/raypolygonseam_test.cpp.
add_executable(gman_raypolygonseam raypolygonseam_test.cpp)
target_link_libraries(gman_raypolygonseam PRIVATE gman gman_ray)

add_test(NAME raypolygonseam COMMAND gman_raypolygonseam)
set_tests_properties(raypolygonseam PROPERTIES
  LABELS unit
  TIMEOUT 30)

# A degenerate Polygon (collinear points) yields no primitive: the factory
# returns an empty owner. See tests/raypolygondegenerate_test.cpp.
add_executable(gman_raypolygondegenerate raypolygondegenerate_test.cpp)
target_link_libraries(gman_raypolygondegenerate PRIVATE gman gman_ray gman_internal_headers)

add_test(NAME raypolygondegenerate COMMAND gman_raypolygondegenerate)
set_tests_properties(raypolygondegenerate PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRayPrimitiveFactory::getRSPolygonMesh returns one internal mesh
# primitive for a PointsPolygons request; GMANRayBVH::build flattens it
# into one entry per face. A ray aimed at a face reports the face's own
# polygon, not the mesh, as hitPrimitive, and a fan of rays across a cube
# agrees, face by face, between the BVH, the mesh's own linear intersect
# and the analytic nearest face. See tests/polygonmeshbvh_test.cpp.
add_executable(gman_polygonmeshbvh polygonmeshbvh_test.cpp)
target_link_libraries(gman_polygonmeshbvh PRIVATE gman gman_ray gman_internal_headers)

add_test(NAME polygonmeshbvh COMMAND gman_polygonmeshbvh)
set_tests_properties(polygonmeshbvh PROPERTIES
  LABELS unit
  TIMEOUT 30)

# A PointsPolygons face's own "s"/"t" is gathered through "verts" out of
# the shared "P" pool, not indexed by local slot --
# tests/rib/pointspolygons_textured.rib's own two faces, cast at each
# one's off-centre local (0.25, 0.3), where a constant, swapped or
# mirrored (s, t) would still fail even though both faces' centroids alone
# could not tell it apart. See tests/polygonmeshtexcoord_test.cpp.
add_executable(gman_polygonmeshtexcoord polygonmeshtexcoord_test.cpp)
target_link_libraries(gman_polygonmeshtexcoord PRIVATE gman gman_ray gman_internal_headers)

add_test(NAME polygonmeshtexcoord COMMAND gman_polygonmeshtexcoord)
set_tests_properties(polygonmeshtexcoord PROPERTIES
  LABELS unit
  TIMEOUT 30)

# A polygon request's vertex or varying "N" shades each hit smoothly: blended
# across the hit's fan triangle, carried to camera space by the inverse
# transpose, oriented into the shader's hemisphere, and refused with one
# warning when its class or length is wrong. See
# tests/raypolygonvertexnormals_test.cpp.
add_executable(gman_raypolygonvertexnormals raypolygonvertexnormals_test.cpp)
target_link_libraries(gman_raypolygonvertexnormals PRIVATE gman gman_ray gman_internal_headers)

add_test(NAME raypolygonvertexnormals COMMAND gman_raypolygonvertexnormals)
set_tests_properties(raypolygonvertexnormals PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANPolygonMesh and its four factory functions: each request's own
# normalisation into faces and loops, its parameter-list sizing, every
# rejection rule, a missing "P", the degenerate shapes the factory still
# accepts, that a mesh owns its own copies, and the type's own
# constructibility. See tests/polygonmesh_test.cpp.
add_executable(gman_polygonmesh polygonmesh_test.cpp)
target_link_libraries(gman_polygonmesh PRIVATE gman gman_internal_headers)

add_test(NAME polygonmesh COMMAND gman_polygonmesh)
set_tests_properties(polygonmesh PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRadiosityMesh: area (analytic and by convergence) and locate()'s own
# round trip. Linked against gman_radiosity_objects alone, with no
# gman_internal_headers: every header it includes is public, proving the
# mesh reaches nothing in libgman/.
add_executable(gman_radiositymesh radiositymesh_test.cpp)
target_link_libraries(gman_radiositymesh PRIVATE gman_radiosity_objects)

add_test(NAME radiositymesh COMMAND gman_radiositymesh)
set_tests_properties(radiositymesh PROPERTIES
  LABELS unit
  TIMEOUT 60)

# GMANRadiosityMesh over a holed GeneralPolygon, a PointsPolygons mesh and
# a PointsGeneralPolygons face with a hole: element areas against the
# analytic outer-minus-hole area, node inside flags around a hole, and
# locate() against hits a GMANRayBVH reports on a mesh face.
add_executable(gman_radiositypolygonmesh radiositypolygonmesh_test.cpp)
target_link_libraries(gman_radiositypolygonmesh PRIVATE gman_radiosity_objects gman_internal_headers)

add_test(NAME radiositypolygonmesh COMMAND gman_radiositypolygonmesh)
set_tests_properties(radiositypolygonmesh PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRadiosityMesh's element accessors: getElementPrimitive against a
# GMANRayBVH hit, surfacePoint against a triangle, a holed polygon and a
# sphere's analytic density, and sameNode across a sphere's seam and poles,
# two coincident spheres and two faces of one mesh. Linked against
# gman_radiosity_objects alone.
add_executable(gman_radiositymeshaccessors radiositymeshaccessors_test.cpp)
target_link_libraries(gman_radiositymeshaccessors PRIVATE gman_radiosity_objects)

add_test(NAME radiositymeshaccessors COMMAND gman_radiositymeshaccessors)
set_tests_properties(radiositymeshaccessors PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRadiositySolver against analytic answers: a closed sphere lit from
# inside, form-factor closure in a cube, transmission through a translucent
# square, two-sided squares, a sphere lit from outside beside a floor, and
# determinism. Linked against gman_radiosity_objects alone.
add_executable(gman_radiositysolver radiositysolver_test.cpp)
target_link_libraries(gman_radiositysolver PRIVATE gman_radiosity_objects)

add_test(NAME radiositysolver COMMAND gman_radiositysolver)
set_tests_properties(radiositysolver PROPERTIES
  LABELS unit
  TIMEOUT 60)

# Counts GMANRadiosityMesh::build's own resolution-search evaluations for
# one primitive driven to kMaxDivisions, and bounds them well below a
# one-step linear search's cubic cost.
add_executable(gman_radiositymeshcost radiositymeshcost_test.cpp)
target_link_libraries(gman_radiositymeshcost PRIVATE gman_radiosity_objects)

add_test(NAME radiositymeshcost COMMAND gman_radiositymeshcost)
set_tests_properties(radiositymeshcost PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANRadiosityPass against a reference built from the same public
# functions it uses internally, over a right-triangle floor with a hole
# and a square wall meeting one of its legs, plus a sphere. Needs only
# gman_radiosity_objects, but solves several times; guarded by
# GMAN_BUILD_RADIOSITY alone, which defaults on, so this runs on
# every CI leg that runs ctest, sanitizers included.
if(GMAN_BUILD_RADIOSITY)
  add_executable(gman_radiositypass radiositypass_test.cpp)
  target_link_libraries(gman_radiositypass PRIVATE gman_radiosity_objects)

  add_test(NAME radiositypass COMMAND gman_radiositypass)
  set_tests_properties(radiositypass PROPERTIES
    LABELS unit
    TIMEOUT 60)

  # The ray tracer renders its rows through gman::parallelFor, one texture
  # cache per worker: an image byte-identical at one, three and eight
  # workers, direct and through the radiosity pass, the worker each row ran
  # on, one missing-texture warning however many workers ran, and a texture only a
  # second reflection reaches and an opacity map only a shadow ray reaches each
  # warning in each of two renders, a wall lit by one disk emitter in an image
  # byte-identical at the same worker counts, and a disk emitter whose radiance
  # holds a NaN adding nothing. Loads
  # libradiosity.so, so it sits in this block. The argument --scaling times
  # the direct scene at one and at eight workers and asserts nothing. See
  # tests/raytracerthreads_test.cpp.
  add_executable(gman_raytracerthreads raytracerthreads_test.cpp)
  target_link_libraries(gman_raytracerthreads PRIVATE gman gman_internal_headers gman_raytracer_objects)
  add_dependencies(gman_raytracerthreads gman_radiosity gman_matte gman_mirror gman_paintedplastic)

  add_test(NAME raytracerthreads COMMAND gman_raytracerthreads)
  set_tests_properties(raytracerthreads PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS unit
    TIMEOUT 30)
endif()

# R5c deferral: the spindle and horn torus (minorradius >= majorradius), a
# second inverse mapping no fixture in this tree needs yet. Pins the wanted
# behavior; WILL_FAIL until the second inverse mapping is implemented.
add_executable(gman_torusspindle torusspindle_test.cpp)
target_link_libraries(gman_torusspindle PRIVATE gman gman_ray)

add_test(NAME torusspindle COMMAND gman_torusspindle)
set_tests_properties(torusspindle PROPERTIES
  WILL_FAIL TRUE
  LABELS unit
  TIMEOUT 30)

# Ray tracer core, unit 3: GMANRayPrimitiveFactory::getRSSphere builds a
# GMANRaySphere holding its parameters and a copied transform, linked
# against gman_ray, where GMANRayPrimitiveFactory and GMANRaySphere
# both live.
add_executable(gman_rayprimitivefactory rayprimitivefactory_test.cpp)
target_link_libraries(gman_rayprimitivefactory PRIVATE gman gman_ray)

add_test(NAME rayprimitivefactory COMMAND gman_rayprimitivefactory)
set_tests_properties(rayprimitivefactory PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANRayPrimitiveFactory::getRSPatch and getRSPatchMesh tessellate each request
# into a watertight triangle mesh over the z-buffer's own dice grid. See
# tests/raypatch_test.cpp.
add_executable(gman_raypatch raypatch_test.cpp)
target_link_libraries(gman_raypatch PRIVATE gman gman_ray)

add_test(NAME raypatch COMMAND gman_raypatch)
set_tests_properties(raypatch PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 240)

# GMANRaytraceRenderer::getDepth, linked against gman_raytracer_objects,
# which still compiles GMANRaytraceRenderer itself. A default
# GMANAttributes loads libmatte.so by bare name, so this carries the
# same ENVIRONMENT as gman_rayprimitivefactory.
add_executable(gman_raydepth raydepth_test.cpp)
target_link_libraries(gman_raydepth PRIVATE gman gman_internal_headers gman_raytracer_objects)

add_test(NAME raydepth COMMAND gman_raydepth)
set_tests_properties(raydepth PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANRayOccluder::transmission, linked against gman_ray, where
# GMANRayOccluder lives; carries raydepth's own ENVIRONMENT for the
# same reason too.
add_executable(gman_rayoccluder rayoccluder_test.cpp)
target_link_libraries(gman_rayoccluder PRIVATE gman gman_ray)

add_test(NAME rayoccluder COMMAND gman_rayoccluder)
set_tests_properties(rayoccluder PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# gman::blockerTransmittance's factor (1 - c) + c * T per channel off a real
# hit, c from the shader's opacity and T from its dielectric lobe; a
# null-shader appearance gives 1 - Os, gman::clampCoverage reads a NaN as
# opaque. See tests/blockertransmittance_test.cpp.
add_executable(gman_blockertransmittance blockertransmittance_test.cpp)
target_link_libraries(gman_blockertransmittance PRIVATE gman gman_ray)

add_test(NAME blockertransmittance COMMAND gman_blockertransmittance)
set_tests_properties(blockertransmittance PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The self-shadow sweep -- every ray primitive, at every scale and
# placement, through its own real intersect() and
# GMANRayOccluder::transmission, linked against gman_ray, where both
# live. Carries raydepth's own ENVIRONMENT for the same reason too.
add_executable(gman_selfshadowsweep selfshadowsweep_test.cpp)
target_link_libraries(gman_selfshadowsweep PRIVATE gman gman_ray)

add_test(NAME selfshadowsweep COMMAND gman_selfshadowsweep)
set_tests_properties(selfshadowsweep PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 60)

# The contact gap -- a small receiver (M ~ max|P|) shadows at a gap the
# old offset hid, a large receiver (M >> max|P|) keeps its own contact
# under the ceiling, linked against gman_ray, where GMANRayOccluder
# lives. Carries raydepth's own ENVIRONMENT for the same reason too.
add_executable(gman_contactgap contactgap_test.cpp)
target_link_libraries(gman_contactgap PRIVATE gman gman_ray)

add_test(NAME contactgap COMMAND gman_contactgap)
set_tests_properties(contactgap PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANRayTracer -- depth-limit termination, background on a miss or at
# the depth limit, single-hit shading matching gman::shade directly,
# the self-shadow offset applied to a traced ray and recursion depth
# counted correctly, and partly opaque layers along a traced ray composited
# front to back. Linked and carries the ENVIRONMENT rayoccluder above does, for
# the same reasons.
add_executable(gman_raytrace raytrace_test.cpp)
target_link_libraries(gman_raytrace PRIVATE gman gman_internal_headers gman_raytracer_objects)

add_test(NAME raytrace COMMAND gman_raytrace)
set_tests_properties(raytrace PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# R-transparency proof, check 5: the composite's own layer cap and the depth
# and alpha a composited sample keeps, linked against gman_raytracer_objects,
# which still compiles GMANRaytraceRenderer itself; carries raydepth's own
# ENVIRONMENT for the same reason too.
add_executable(gman_compositelayercap compositelayercap_test.cpp)
target_link_libraries(gman_compositelayercap PRIVATE gman gman_internal_headers gman_raytracer_objects)

add_test(NAME compositelayercap COMMAND gman_compositelayercap)
set_tests_properties(compositelayercap PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANRayOccluder::transmission's own iteration cap, linked against
# gman_ray, where GMANRayOccluder lives. Carries raydepth's own
# ENVIRONMENT for the same reason too. See
# coincidenttransmission_test.cpp's own comment.
add_executable(gman_coincidenttransmission coincidenttransmission_test.cpp)
target_link_libraries(gman_coincidenttransmission PRIVATE gman gman_ray)

add_test(NAME coincidenttransmission COMMAND gman_coincidenttransmission)
set_tests_properties(coincidenttransmission PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Two close, camera-facing transparent disks, linked against
# gman_raytracer_objects, which still compiles GMANRaytraceRenderer
# itself; carries raydepth's own ENVIRONMENT for the same reason too.
# See closetransparentstack_test.cpp's own comment.
add_executable(gman_closetransparentstack closetransparentstack_test.cpp)
target_link_libraries(gman_closetransparentstack PRIVATE gman gman_internal_headers gman_raytracer_objects)

add_test(NAME closetransparentstack COMMAND gman_closetransparentstack)
set_tests_properties(closetransparentstack PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# A grazing transparent hit, linked against gman_raytracer_objects,
# which still compiles GMANRaytraceRenderer itself; carries raydepth's
# own ENVIRONMENT for the same reason too. See
# grazingtransparent_test.cpp's own comment.
add_executable(gman_grazingtransparent grazingtransparent_test.cpp)
target_link_libraries(gman_grazingtransparent PRIVATE gman gman_internal_headers gman_raytracer_objects)

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
  target_link_libraries(gman_rayrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_rayrender gman_raytracer)

  set(GMAN_RAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/rayrender-run")
  file(MAKE_DIRECTORY "${GMAN_RAYRENDER_WORKDIR}")

  add_test(NAME rayrender
    COMMAND gman_rayrender $<TARGET_FILE:gman_cli>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_RAYRENDER_WORKDIR}")
  set_tests_properties(rayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # DefaultBGColor is the RISpec's black: an ambient-only scene with a
  # small sphere leaves every corner of an 8x8 frame uncovered, reading
  # exactly (0, 0, 0) under both renderers. See tests/backgroundcolor_test.cpp.
  add_executable(gman_backgroundcolor backgroundcolor_test.cpp)
  target_link_libraries(gman_backgroundcolor PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_backgroundcolor gman_raytracer)

  set(GMAN_BACKGROUNDCOLOR_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/backgroundcolor-run")
  file(MAKE_DIRECTORY "${GMAN_BACKGROUNDCOLOR_WORKDIR}")

  add_test(NAME backgroundcolor
    COMMAND gman_backgroundcolor $<TARGET_FILE:gman_cli>
    WORKING_DIRECTORY "${GMAN_BACKGROUNDCOLOR_WORKDIR}")
  set_tests_properties(backgroundcolor PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # `gman -r gmanraytracer` renders tests/rib/r5a_disk.rib, a
  # partial-thetamax disk, matching a checked-in golden image. See
  # tests/diskrayrender_test.cpp.
  add_executable(gman_diskrayrender diskrayrender_test.cpp)
  target_link_libraries(gman_diskrayrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_diskrayrender gman_raytracer)

  set(GMAN_DISKRAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/diskrayrender-run")
  file(MAKE_DIRECTORY "${GMAN_DISKRAYRENDER_WORKDIR}")

  add_test(NAME diskrayrender
    COMMAND gman_diskrayrender $<TARGET_FILE:gman_cli>
                               "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_DISKRAYRENDER_WORKDIR}")
  set_tests_properties(diskrayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # `gman -r gmanraytracer` renders tests/rib/r5b_quadrics.rib,
  # a cylinder, cone, paraboloid and hyperboloid side by side, matching a
  # checked-in golden image. See tests/quadricsrayrender_test.cpp.
  add_executable(gman_quadricsrayrender quadricsrayrender_test.cpp)
  target_link_libraries(gman_quadricsrayrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_quadricsrayrender gman_raytracer)

  set(GMAN_QUADRICSRAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/quadricsrayrender-run")
  file(MAKE_DIRECTORY "${GMAN_QUADRICSRAYRENDER_WORKDIR}")

  add_test(NAME quadricsrayrender
    COMMAND gman_quadricsrayrender $<TARGET_FILE:gman_cli>
                                   "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_QUADRICSRAYRENDER_WORKDIR}")
  set_tests_properties(quadricsrayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # `gman -r gmanraytracer` renders tests/rib/r8_mirror.rib -- a mirror
  # sphere reflecting a sphere placed behind the camera -- matching a
  # checked-in golden image, and the reflected colour appears nowhere in
  # tests/rib/r8_mirror_matte.rib's render, the same scene with the
  # mirror sphere replaced by matte. See tests/mirrorrayrender_test.cpp.
  add_executable(gman_mirrorrayrender mirrorrayrender_test.cpp)
  target_link_libraries(gman_mirrorrayrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_mirrorrayrender gman_raytracer gman_mirror gman_matte)

  set(GMAN_MIRRORRAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/mirrorrayrender-run")
  file(MAKE_DIRECTORY "${GMAN_MIRRORRAYRENDER_WORKDIR}")

  add_test(NAME mirrorrayrender
    COMMAND gman_mirrorrayrender $<TARGET_FILE:gman_cli>
                                 "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_MIRRORRAYRENDER_WORKDIR}")
  set_tests_properties(mirrorrayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # `gman -r gmanraytracer` renders tests/rib/r8_glass.rib -- a glass
  # sphere in front of a two-tone backdrop -- matching a checked-in
  # golden image, and a horizontal scanline through the sphere shows the
  # backdrop's edge displaced from its unrefracted raster x. See
  # tests/glassrayrender_test.cpp.
  add_executable(gman_glassrayrender glassrayrender_test.cpp)
  target_link_libraries(gman_glassrayrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_glassrayrender gman_raytracer gman_glass gman_matte)

  set(GMAN_GLASSRAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/glassrayrender-run")
  file(MAKE_DIRECTORY "${GMAN_GLASSRAYRENDER_WORKDIR}")

  add_test(NAME glassrayrender
    COMMAND gman_glassrayrender $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_GLASSRAYRENDER_WORKDIR}")
  set_tests_properties(glassrayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # `gman -r gmanraytracer` renders tests/rib/indirectseam_*.rib: Option
  # "render" "string indirect" naming constantindirect brightens a matte
  # sphere by exactly that module's constant, a missing pass changes
  # nothing, clearing the name undoes it, and the pass reaches a mirror's
  # reflected hit too. See tests/indirectrender_test.cpp.
  add_executable(gman_indirectrender indirectrender_test.cpp)
  target_link_libraries(gman_indirectrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_indirectrender gman_raytracer gman_matte gman_mirror gman_constantindirect)

  set(GMAN_INDIRECTRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/indirectrender-run")
  file(MAKE_DIRECTORY "${GMAN_INDIRECTRENDER_WORKDIR}")

  add_test(NAME indirectrender
    COMMAND gman_indirectrender $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_INDIRECTRENDER_WORKDIR}")
  set_tests_properties(indirectrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # `gman -r gmanraytracer` renders tests/rib/radiosity_box*.rib and
  # tests/rib/radiosity_sphere*.rib: the radiosity pass brightens a closed
  # box's shadowed side without ever dimming a pixel, two renders of
  # that scene are byte-identical, and a lone sphere renders exactly as it
  # does with no pass at all. Needs the plugin itself, guarded by
  # GMAN_BUILD_RADIOSITY alone. See tests/radiosityrender_test.cpp.
  if(GMAN_BUILD_RADIOSITY)
    add_executable(gman_radiosityrender radiosityrender_test.cpp)
    target_link_libraries(gman_radiosityrender PRIVATE gman TIFF::TIFF)
    add_dependencies(gman_radiosityrender gman_raytracer gman_radiosity gman_matte)

    set(GMAN_RADIOSITYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/radiosityrender-run")
    file(MAKE_DIRECTORY "${GMAN_RADIOSITYRENDER_WORKDIR}")

    add_test(NAME radiosityrender
      COMMAND gman_radiosityrender $<TARGET_FILE:gman_cli>
                                   "${CMAKE_CURRENT_SOURCE_DIR}/rib"
      WORKING_DIRECTORY "${GMAN_RADIOSITYRENDER_WORKDIR}")
    set_tests_properties(radiosityrender PROPERTIES
      ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
      LABELS render
      TIMEOUT 60)

    # `gman -r gmanraytracer` renders tests/rib/cornellbox.rib and
    # tests/rib/cornellbox_direct.rib: the radiosity pass bleeds colour
    # from each side wall onto the back wall beside it, naming no pass
    # leaves the back wall symmetric, and two renders of the pass's own
    # scene are byte-identical. See tests/cornellbox_test.cpp.
    add_executable(gman_cornellbox cornellbox_test.cpp)
    target_link_libraries(gman_cornellbox PRIVATE gman TIFF::TIFF)
    add_dependencies(gman_cornellbox gman_raytracer gman_radiosity gman_matte)

    set(GMAN_CORNELLBOX_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/cornellbox-run")
    file(MAKE_DIRECTORY "${GMAN_CORNELLBOX_WORKDIR}")

    add_test(NAME cornellbox
      COMMAND gman_cornellbox $<TARGET_FILE:gman_cli>
                             "${CMAKE_CURRENT_SOURCE_DIR}/rib"
      WORKING_DIRECTORY "${GMAN_CORNELLBOX_WORKDIR}")
    set_tests_properties(cornellbox PROPERTIES
      ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
      LABELS render
      TIMEOUT 30)

    # `gman -r gmanraytracer` renders tests/rib/radiositywindow.rib,
    # tests/rib/radiositywindow_opaque.rib and
    # tests/rib/radiositywindow_direct.rib: the radiosity pass, not the
    # ray tracer's own direct-light transmission through the window
    # pane, brightens a ceiling that a downward-aimed distantlight cannot
    # reach directly. See tests/radiositywindow_test.cpp.
    add_executable(gman_radiositywindow radiositywindow_test.cpp)
    target_link_libraries(gman_radiositywindow PRIVATE gman TIFF::TIFF)
    add_dependencies(gman_radiositywindow gman_raytracer gman_radiosity gman_matte)

    set(GMAN_RADIOSITYWINDOW_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/radiositywindow-run")
    file(MAKE_DIRECTORY "${GMAN_RADIOSITYWINDOW_WORKDIR}")

    add_test(NAME radiositywindow
      COMMAND gman_radiositywindow $<TARGET_FILE:gman_cli>
                                   "${CMAKE_CURRENT_SOURCE_DIR}/rib"
      WORKING_DIRECTORY "${GMAN_RADIOSITYWINDOW_WORKDIR}")
    set_tests_properties(radiositywindow PROPERTIES
      ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
      LABELS render
      TIMEOUT 30)

    # `gman -r gmanraytracer` renders tests/rib/radiosity_rim.rib, the
    # lower-left rim of a matte sphere in a lit room: the pass picks each
    # element's side from the hit's own normal, so the rim shows no black
    # arc where the diced facet's chord normal falls across the ray. See
    # tests/radiosityrim_test.cpp.
    add_executable(gman_radiosityrim radiosityrim_test.cpp)
    target_link_libraries(gman_radiosityrim PRIVATE gman TIFF::TIFF)
    add_dependencies(gman_radiosityrim gman_raytracer gman_radiosity gman_matte)

    set(GMAN_RADIOSITYRIM_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/radiosityrim-run")
    file(MAKE_DIRECTORY "${GMAN_RADIOSITYRIM_WORKDIR}")

    add_test(NAME radiosityrim
      COMMAND gman_radiosityrim $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
      WORKING_DIRECTORY "${GMAN_RADIOSITYRIM_WORKDIR}")
    set_tests_properties(radiosityrim PROPERTIES
      ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
      LABELS render
      TIMEOUT 30)
  endif()

  # `gman -r gmanraytracer` renders tests/rib/r5c_torus.rib, a
  # full torus tilted so its hole shows and a partial torus, matching a
  # checked-in golden image. See tests/torusrayrender_test.cpp.
  add_executable(gman_torusrayrender torusrayrender_test.cpp)
  target_link_libraries(gman_torusrayrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_torusrayrender gman_raytracer)

  set(GMAN_TORUSRAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/torusrayrender-run")
  file(MAKE_DIRECTORY "${GMAN_TORUSRAYRENDER_WORKDIR}")

  add_test(NAME torusrayrender
    COMMAND gman_torusrayrender $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_TORUSRAYRENDER_WORKDIR}")
  set_tests_properties(torusrayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # `gman -r gmanraytracer` renders tests/rib/r5a_polygon.rib, a
  # camera-facing pentagon, matching a checked-in golden image. See
  # tests/polygonrayrender_test.cpp.
  add_executable(gman_polygonrayrender polygonrayrender_test.cpp)
  target_link_libraries(gman_polygonrayrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_polygonrayrender gman_raytracer)

  set(GMAN_POLYGONRAYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/polygonrayrender-run")
  file(MAKE_DIRECTORY "${GMAN_POLYGONRAYRENDER_WORKDIR}")

  add_test(NAME polygonrayrender
    COMMAND gman_polygonrayrender $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_POLYGONRAYRENDER_WORKDIR}")
  set_tests_properties(polygonrayrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # `gman -r gmanraytracer` renders tests/rib/raypolygonseam.rib, an
  # open-front box whose ceiling and right wall share an edge on the screen
  # diagonal, with every pixel opaque. See
  # tests/raypolygonseamrender_test.cpp.
  add_executable(gman_raypolygonseamrender raypolygonseamrender_test.cpp)
  target_link_libraries(gman_raypolygonseamrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_raypolygonseamrender gman_raytracer)

  set(GMAN_RAYPOLYGONSEAMRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/raypolygonseamrender-run")
  file(MAKE_DIRECTORY "${GMAN_RAYPOLYGONSEAMRENDER_WORKDIR}")

  add_test(NAME raypolygonseamrender
    COMMAND gman_raypolygonseamrender $<TARGET_FILE:gman_cli>
                                      "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_RAYPOLYGONSEAMRENDER_WORKDIR}")
  set_tests_properties(raypolygonseamrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # The same tests/rib/r5a_polygon.rib fixture, rendered by both
  # renderers and compared away from the silhouette edge, where the two
  # renderers' differing sample placement is expected to disagree. See
  # tests/polygonzbuffer_test.cpp.
  add_executable(gman_polygonzbuffer polygonzbuffer_test.cpp)
  target_link_libraries(gman_polygonzbuffer PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_polygonzbuffer gman_raytracer)

  set(GMAN_POLYGONZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/polygonzbuffer-run")
  file(MAKE_DIRECTORY "${GMAN_POLYGONZBUFFER_WORKDIR}")

  add_test(NAME polygonzbuffer
    COMMAND gman_polygonzbuffer $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_POLYGONZBUFFER_WORKDIR}")
  set_tests_properties(polygonzbuffer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # GMANRayPolygon::intersect resolves and interpolates "st" the way
  # GMANRasterPrimitiveFactory's own polygon rule does: pinned against the
  # checker texture's own texels, then cross-checked against the z-buffer's
  # own polygon output on a texture affine in (s, t). See
  # tests/polygonstparity_test.cpp.
  add_executable(gman_polygonstparity polygonstparity_test.cpp)
  target_link_libraries(gman_polygonstparity PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_polygonstparity gman_raytracer)

  set(GMAN_POLYGONSTPARITY_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/polygonstparity-run")
  file(MAKE_DIRECTORY "${GMAN_POLYGONSTPARITY_WORKDIR}")

  add_test(NAME polygonstparity
    COMMAND gman_polygonstparity $<TARGET_FILE:gman_cli>
                                 "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_POLYGONSTPARITY_WORKDIR}")
  set_tests_properties(polygonstparity PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # GeneralPolygon, PointsPolygons and PointsGeneralPolygons render with
  # the z-buffer's own coverage -- tests/rib/generalpolygon_hole_twin.rib,
  # pointsgeneralpolygons_hole.rib, pointspolygons_cube.rib and
  # pointspolygons_textured.rib, each rendered under both renderers and
  # compared pixel by pixel on alpha coverage. See
  # tests/polygonraycoverage_test.cpp.
  add_executable(gman_polygonraycoverage polygonraycoverage_test.cpp)
  target_link_libraries(gman_polygonraycoverage PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_polygonraycoverage gman_raytracer)

  set(GMAN_POLYGONRAYCOVERAGE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/polygonraycoverage-run")
  file(MAKE_DIRECTORY "${GMAN_POLYGONRAYCOVERAGE_WORKDIR}")

  add_test(NAME polygonraycoverage
    COMMAND gman_polygonraycoverage $<TARGET_FILE:gman_cli>
                                    "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_POLYGONRAYCOVERAGE_WORKDIR}")
  set_tests_properties(polygonraycoverage PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 60)

  # tests/rib/shadow.rib -- a small sphere between a distant light and a
  # large receiver -- casts a shadow a checked-in golden pins, and one
  # pixel inside it holds two further relations against
  # shadow_nolight.rib and shadow_noblocker.rib. See
  # shadowrender_test.cpp's own comment.
  add_executable(gman_shadowrender shadowrender_test.cpp)
  target_link_libraries(gman_shadowrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_shadowrender gman_raytracer)

  set(GMAN_SHADOWRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/shadowrender-run")
  file(MAKE_DIRECTORY "${GMAN_SHADOWRENDER_WORKDIR}")

  add_test(NAME shadowrender
    COMMAND gman_shadowrender $<TARGET_FILE:gman_cli>
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
  target_link_libraries(gman_transparencyrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_transparencyrender gman_raytracer)

  set(GMAN_TRANSPARENCYRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/transparencyrender-run")
  file(MAKE_DIRECTORY "${GMAN_TRANSPARENCYRENDER_WORKDIR}")

  add_test(NAME transparencyrender
    COMMAND gman_transparencyrender $<TARGET_FILE:gman_cli>
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
  target_link_libraries(gman_transparencyshadowrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_transparencyshadowrender gman_raytracer)

  set(GMAN_TRANSPARENCYSHADOWRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/transparencyshadowrender-run")
  file(MAKE_DIRECTORY "${GMAN_TRANSPARENCYSHADOWRENDER_WORKDIR}")

  add_test(NAME transparencyshadowrender
    COMMAND gman_transparencyshadowrender $<TARGET_FILE:gman_cli>
                                          "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_TRANSPARENCYSHADOWRENDER_WORKDIR}")
  set_tests_properties(transparencyshadowrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # tests/rib/contactshadow.rib -- a thin vertical blocker resting on a
  # horizontal floor at gap 0.001 -- against contactshadow_noblocker.rib,
  # pinning that the receiver strip beside the blocker's base darkens
  # once the shadow ray's origin offset lands. See
  # contactshadow_test.cpp's own comment.
  add_executable(gman_contactshadow contactshadow_test.cpp)
  target_link_libraries(gman_contactshadow PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_contactshadow gman_raytracer)

  set(GMAN_CONTACTSHADOW_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/contactshadow-run")
  file(MAKE_DIRECTORY "${GMAN_CONTACTSHADOW_WORKDIR}")

  add_test(NAME contactshadow
    COMMAND gman_contactshadow $<TARGET_FILE:gman_cli>
                               "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_CONTACTSHADOW_WORKDIR}")
  set_tests_properties(contactshadow PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # tests/rib/acnesphere_x1.rib, acnesphere_x1000.rib and
  # acnesphere_xsmall.rib -- one lit sphere at three scales -- pinning
  # that self-shadow acne is scale-independent and that no pixel inside
  # the silhouette dips below both its horizontal neighbors. See
  # acnesphere_test.cpp's own comment.
  add_executable(gman_acnesphere acnesphere_test.cpp)
  target_link_libraries(gman_acnesphere PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_acnesphere gman_raytracer)

  set(GMAN_ACNESPHERE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/acnesphere-run")
  file(MAKE_DIRECTORY "${GMAN_ACNESPHERE_WORKDIR}")

  add_test(NAME acnesphere
    COMMAND gman_acnesphere $<TARGET_FILE:gman_cli>
                            "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_ACNESPHERE_WORKDIR}")
  set_tests_properties(acnesphere PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # tests/rib/surfaceparameters_two_surfaces.rib,
  # surfaceparameters_restored.rib and surfaceparameters_default.rib pin
  # that every surface shades with its own RiSurface's parameter list, one
  # fixture per form of the defect. See surfaceparameters_test.cpp's own
  # comment.
  add_executable(gman_surfaceparameters surfaceparameters_test.cpp)
  target_link_libraries(gman_surfaceparameters PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_surfaceparameters gman_raytracer)

  set(GMAN_SURFACEPARAMETERS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/surfaceparameters-run")
  file(MAKE_DIRECTORY "${GMAN_SURFACEPARAMETERS_WORKDIR}")

  add_test(NAME surfaceparameters
    COMMAND gman_surfaceparameters $<TARGET_FILE:gman_cli>
                                   "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_SURFACEPARAMETERS_WORKDIR}")
  set_tests_properties(surfaceparameters PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)

  # Coverage-alpha proof: `gman -r gmanzbuffer` (default) and `-r
  # gmanraytracer` each track per-sample coverage into OutputTIFF's own
  # alpha byte -- an uncovered pixel reads 0, a fully-covered bright pixel
  # reads 255, a silhouette-straddling pixel reads an intermediate value
  # tracking its own colour-implied coverage fraction, and a uniformly
  # semi-transparent surface's pixel reads its own Opacity, every asserted
  # pixel's RGB at or below its own alpha. See coveragealpha_test.cpp's own
  # comment.
  add_executable(gman_coveragealpha coveragealpha_test.cpp)
  target_link_libraries(gman_coveragealpha PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_coveragealpha gman_raytracer)

  set(GMAN_COVERAGEALPHA_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/coveragealpha-run")
  file(MAKE_DIRECTORY "${GMAN_COVERAGEALPHA_WORKDIR}")

  add_test(NAME coveragealpha
    COMMAND gman_coveragealpha $<TARGET_FILE:gman_cli>
                               "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_COVERAGEALPHA_WORKDIR}")
  set_tests_properties(coveragealpha PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# A Surface naming a module that will not load -- missing entirely, opening
# but exporting no shader, exporting a shader that is not a surface, or
# missing GMANDestroyShader -- reports RIE_NOSHADER at RIE_ERROR, which
# stops the file: exit 1, no image, the report naming the failing Surface.
# A control scene without the bad Surface proves the renderer itself still
# works. See unknownsurface_test.cpp's own comment. Both renderers write
# the same TIFF names, so each gets its own WORKING_DIRECTORY.
add_executable(gman_unknownsurface unknownsurface_test.cpp)
target_link_libraries(gman_unknownsurface PRIVATE gman TIFF::TIFF)
add_dependencies(gman_unknownsurface gman_zbuffer gman_matte gman_plastic gman_notasurface gman_nodestroyshader)

set(GMAN_UNKNOWNSURFACE_ZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/unknownsurface-zbuffer-run")
file(MAKE_DIRECTORY "${GMAN_UNKNOWNSURFACE_ZBUFFER_WORKDIR}")

add_test(NAME unknownsurface_zbuffer
  COMMAND gman_unknownsurface $<TARGET_FILE:gman_cli>
                              "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                              gmanzbuffer
  WORKING_DIRECTORY "${GMAN_UNKNOWNSURFACE_ZBUFFER_WORKDIR}")
set_tests_properties(unknownsurface_zbuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_unknownsurface gman_raytracer)

  set(GMAN_UNKNOWNSURFACE_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/unknownsurface-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_UNKNOWNSURFACE_RAYTRACER_WORKDIR}")

  add_test(NAME unknownsurface_raytracer
    COMMAND gman_unknownsurface $<TARGET_FILE:gman_cli>
                                "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                gmanraytracer
    WORKING_DIRECTORY "${GMAN_UNKNOWNSURFACE_RAYTRACER_WORKDIR}")
  set_tests_properties(unknownsurface_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# A RIB Atmosphere, Displacement or Imager reaches the renderer as the C
# API's call does: a missing shader stops the file with RIE_NOSHADER, a
# loadable one renders the control scene's pixels. See
# ribshaderrequests_test.cpp's own comment. Both renderers write the same
# TIFF names, so each gets its own WORKING_DIRECTORY.
add_executable(gman_ribshaderrequests ribshaderrequests_test.cpp)
target_link_libraries(gman_ribshaderrequests PRIVATE gman TIFF::TIFF)
add_dependencies(gman_ribshaderrequests gman_zbuffer gman_countingvolume gman_identitydisplacement
  gman_countingimager)

set(GMAN_RIBSHADERREQUESTS_ZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/ribshaderrequests-zbuffer-run")
file(MAKE_DIRECTORY "${GMAN_RIBSHADERREQUESTS_ZBUFFER_WORKDIR}")

add_test(NAME ribshaderrequests_zbuffer
  COMMAND gman_ribshaderrequests $<TARGET_FILE:gman_cli>
                                 "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                 gmanzbuffer
  WORKING_DIRECTORY "${GMAN_RIBSHADERREQUESTS_ZBUFFER_WORKDIR}")
set_tests_properties(ribshaderrequests_zbuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_ribshaderrequests gman_raytracer)

  set(GMAN_RIBSHADERREQUESTS_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/ribshaderrequests-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_RIBSHADERREQUESTS_RAYTRACER_WORKDIR}")

  add_test(NAME ribshaderrequests_raytracer
    COMMAND gman_ribshaderrequests $<TARGET_FILE:gman_cli>
                                   "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                                   gmanraytracer
    WORKING_DIRECTORY "${GMAN_RIBSHADERREQUESTS_RAYTRACER_WORKDIR}")
  set_tests_properties(ribshaderrequests_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# gman's own error handler prints every report exactly as RiErrorPrint
# does, then stops the file at RIE_ERROR or above: exit 1, no image, the
# next file on the command line still runs. See reportederror_test.cpp's
# own comment.
add_executable(gman_reportederror reportederror_test.cpp)
target_link_libraries(gman_reportederror PRIVATE gman TIFF::TIFF)
add_dependencies(gman_reportederror gman_zbuffer gman_matte)

set(GMAN_REPORTEDERROR_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/reportederror-run")
file(MAKE_DIRECTORY "${GMAN_REPORTEDERROR_WORKDIR}")

add_test(NAME reportederror
  COMMAND gman_reportederror $<TARGET_FILE:gman_cli>
                             "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_REPORTEDERROR_WORKDIR}")
set_tests_properties(reportederror PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# GMANOutput::save's own pipeline -- gain and gamma on colour, then every
# channel rounded and dithered by RISpec's Quantize rule and clamped into
# the request's range -- and what gman::OutputPNM, gman::OutputTIFF and
# gman::OutputPNG do with the integer samples save hands their writeImage.
# Direct unit test against those classes, no gman binary. The PNG checks
# compile and link only under GMAN_WITH_PNG.
add_executable(gman_outputnarrowing outputnarrowing_test.cpp)
target_link_libraries(gman_outputnarrowing PRIVATE gman TIFF::TIFF gman_internal_headers)
if(GMAN_WITH_PNG)
  target_compile_definitions(gman_outputnarrowing PRIVATE GMAN_WITH_PNG)
  target_link_libraries(gman_outputnarrowing PRIVATE PNG::PNG)
endif()

set(GMAN_OUTPUTNARROWING_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/outputnarrowing-run")
file(MAKE_DIRECTORY "${GMAN_OUTPUTNARROWING_WORKDIR}")

add_test(NAME outputnarrowing
  COMMAND gman_outputnarrowing
  WORKING_DIRECTORY "${GMAN_OUTPUTNARROWING_WORKDIR}")
set_tests_properties(outputnarrowing PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::OutputPNG's own DisplayMode contract: "rgb" writes three samples
# with no alpha, "rgbaz" keeps four with coverage in the fourth, "a"
# writes the coverage byte alone, and every mode but "rgb" writes straight
# alpha. Direct unit test against gman::OutputPNG, no gman binary.
# Compiles and links only under GMAN_WITH_PNG.
add_executable(gman_pngdisplaymode pngdisplaymode_test.cpp)
target_link_libraries(gman_pngdisplaymode PRIVATE gman gman_internal_headers)
if(GMAN_WITH_PNG)
  target_compile_definitions(gman_pngdisplaymode PRIVATE GMAN_WITH_PNG)
  target_link_libraries(gman_pngdisplaymode PRIVATE PNG::PNG)
endif()

set(GMAN_PNGDISPLAYMODE_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/pngdisplaymode-run")
file(MAKE_DIRECTORY "${GMAN_PNGDISPLAYMODE_WORKDIR}")

add_test(NAME pngdisplaymode
  COMMAND gman_pngdisplaymode
  WORKING_DIRECTORY "${GMAN_PNGDISPLAYMODE_WORKDIR}")
set_tests_properties(pngdisplaymode PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::OutputPNM and gman::OutputPNG write 16-bit samples, most significant
# byte first, when Quantize's max exceeds 255 and 8-bit samples otherwise,
# and a max above 65535 stops the file. Direct unit test against those
# classes, no gman binary. The PNG checks compile and link only under
# GMAN_WITH_PNG.
add_executable(gman_output16 output16_test.cpp)
target_link_libraries(gman_output16 PRIVATE gman gman_internal_headers)
if(GMAN_WITH_PNG)
  target_compile_definitions(gman_output16 PRIVATE GMAN_WITH_PNG)
  target_link_libraries(gman_output16 PRIVATE PNG::PNG)
endif()

set(GMAN_OUTPUT16_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/output16-run")
file(MAKE_DIRECTORY "${GMAN_OUTPUT16_WORKDIR}")

add_test(NAME output16
  COMMAND gman_output16
  WORKING_DIRECTORY "${GMAN_OUTPUT16_WORKDIR}")
set_tests_properties(output16 PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::OutputPNG writes no gAMA chunk at any Exposure gamma, so every
# viewer displays its bytes as written.
add_executable(gman_pnggamma pnggamma_test.cpp)
target_link_libraries(gman_pnggamma PRIVATE gman gman_internal_headers)
if(GMAN_WITH_PNG)
  target_compile_definitions(gman_pnggamma PRIVATE GMAN_WITH_PNG)
  target_link_libraries(gman_pnggamma PRIVATE PNG::PNG)
endif()

set(GMAN_PNGGAMMA_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/pnggamma-run")
file(MAKE_DIRECTORY "${GMAN_PNGGAMMA_WORKDIR}")

add_test(NAME pnggamma
  COMMAND gman_pnggamma
  WORKING_DIRECTORY "${GMAN_PNGGAMMA_WORKDIR}")
set_tests_properties(pnggamma PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::sampleHash, gman::unitFloat, gman::sample1D and gman::sample2D: a
# counter-based sampler that is a pure function of its arguments, so call
# order leaves every draw unchanged. The schedule independence check
# drives gman::parallelFor directly.
add_executable(gman_sampler sampler_test.cpp)
target_link_libraries(gman_sampler PRIVATE gman)

add_test(NAME sampler COMMAND gman_sampler)
set_tests_properties(sampler PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman's direction warps, their pdfs and the tangent frame. Domain
# checks are driven by gman::sample2D; the statistics draw i.i.d.
# samples so their standard error is the real one.
add_executable(gman_samplewarp samplewarp_test.cpp)
target_link_libraries(gman_samplewarp PRIVATE gman)

add_test(NAME samplewarp COMMAND gman_samplewarp)
set_tests_properties(samplewarp PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::BSDF and its Lambert lobe: energy, reciprocity, sidedness, sample
# against pdf by histogram, clamping, capacity and no heap allocation.
# The statistics draw i.i.d. samples, as samplewarp's do. Links
# raybvhallocator.cpp for its counting operator new.
add_executable(gman_bsdf bsdf_test.cpp raybvhallocator.cpp)
target_link_libraries(gman_bsdf PRIVATE gman)

add_test(NAME bsdf COMMAND gman_bsdf)
set_tests_properties(bsdf PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::BSDF's GGX lobe: a double-precision reference, reciprocity,
# sidedness, the eval underflow guard, clamping, capacity and no heap
# allocation. Links raybvhallocator.cpp as bsdf's own test does.
add_executable(gman_bsdfggx bsdfggx_test.cpp raybvhallocator.cpp)
target_link_libraries(gman_bsdfggx PRIVATE gman)

add_test(NAME bsdfggx COMMAND gman_bsdfggx)
set_tests_properties(bsdfggx PROPERTIES
  LABELS unit
  TIMEOUT 60)

# gman::BSDF's GGX lobe's statistics: energy by quadrature and by sample(),
# sample() against pdf() by histogram, and a two-lobe mixture.
add_executable(gman_bsdfggxstats bsdfggxstats_test.cpp)
target_link_libraries(gman_bsdfggxstats PRIVATE gman)

add_test(NAME bsdfggxstats COMMAND gman_bsdfggxstats)
set_tests_properties(bsdfggxstats PROPERTIES
  LABELS unit
  TIMEOUT 60)

# gman::BSDF's mirror and dielectric delta lobes: a double-precision
# reference for exact Fresnel, reflection and Snell refraction; direction,
# Fresnel, energy and reciprocity; a three-lobe mixture; clamping, eta
# bounds, capacity and failed draws; shadowTransmittance; and no heap
# allocation. Links raybvhallocator.cpp as bsdf's own test does.
add_executable(gman_bsdfdelta bsdfdelta_test.cpp raybvhallocator.cpp)
target_link_libraries(gman_bsdfdelta PRIVATE gman)

add_test(NAME bsdfdelta COMMAND gman_bsdfdelta)
set_tests_properties(bsdfdelta PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Each executable that replaces the global operator new exports it, so the
# libraries it links bind the replacement under link-time optimization on
# Mach-O; ELF exports it already.
foreach(counting_allocator_test
    gman_paramlist gman_raybvhalloc gman_bsdf gman_bsdfggx gman_bsdfdelta)
  target_link_options(${counting_allocator_test} PRIVATE
    "$<$<PLATFORM_ID:Darwin>:LINKER:-export_dynamic>")
endforeach()

# gman::bsdf, the shading API's free function: plastic matches a hand-built
# env, matte builds at the shading normal, a null shader answers Cs as one
# Lambert lobe, and glass answers its dielectric lobe. Shaders load through
# GMANAttributes::setSurface, as shaderbsdf_test.cpp's do. Links no path
# tracer code; the include path alone lets pathtracerscene.h's shared
# helpers, unused here, still compile.
add_executable(gman_hitbsdf hitbsdf_test.cpp)
target_link_libraries(gman_hitbsdf PRIVATE gman gman_ray gman_internal_headers)
target_include_directories(gman_hitbsdf PRIVATE
  $<TARGET_PROPERTY:gman_pathtracer_objects,INTERFACE_INCLUDE_DIRECTORIES>)
add_dependencies(gman_hitbsdf gman_matte gman_plastic gman_glass)

add_test(NAME hitbsdf COMMAND gman_hitbsdf)
set_tests_properties(hitbsdf PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# RiOptionV's handling of Option "pathtracer" "integer samples": unset reads 4,
# a later Option replaces an earlier one, a value below 1 leaves it
# unchanged but logs a warning, and an unrelated token or Option name --
# "render" "string indirect" and "radiosity" "float elementsize" included --
# changes nothing. See tests/pathtraceroption_test.cpp.
add_executable(gman_pathtraceroption pathtraceroption_test.cpp)
target_link_libraries(gman_pathtraceroption PRIVATE gman gman_internal_headers)

add_test(NAME pathtraceroption COMMAND gman_pathtraceroption)
set_tests_properties(pathtraceroption PROPERTIES
  LABELS unit
  TIMEOUT 30)

# The option store: RiOptionV keeps every Option parameter the typed
# handlers do not read, readable by Option name and parameter identifier.
# Each storage type round-trips, a later value replaces and another
# parameter accumulates, values are owned, the three pairs the typed
# handlers read stay out, an unresolvable token drops silently, and a
# frame's Options end with it. See tests/optionstore_test.cpp.
add_executable(gman_optionstore optionstore_test.cpp)
target_link_libraries(gman_optionstore PRIVATE gman gman_internal_headers)

add_test(NAME optionstore COMMAND gman_optionstore)
set_tests_properties(optionstore PROPERTIES
  LABELS unit
  TIMEOUT 30)

# The sphere furnace: a point light at the centre of a closed diffuse
# sphere converges to rho * I / (R^2 * (1 - rho)), and the estimate's
# standard deviation falls as more paths average per slot. Linked against
# gman_pathtracer_objects, which still compiles GMANPathtraceRenderer
# itself. See tests/pathtracerfurnace_test.cpp.
add_executable(gman_pathtracerfurnace pathtracerfurnace_test.cpp)
target_link_libraries(gman_pathtracerfurnace PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerfurnace gman_matte)

add_test(NAME pathtracerfurnace COMMAND gman_pathtracerfurnace)
set_tests_properties(pathtracerfurnace PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Next-event estimation on the floor scene, one light at a time: a point,
# a spot and a distant light each match rho * Cl(p) * cos(theta). Linked
# against gman_pathtracer_objects, which still compiles
# GMANPathtraceRenderer itself. See tests/pathtracerdirect_test.cpp.
add_executable(gman_pathtracerdirect pathtracerdirect_test.cpp)
target_link_libraries(gman_pathtracerdirect PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerdirect gman_matte)

add_test(NAME pathtracerdirect COMMAND gman_pathtracerdirect)
set_tests_properties(pathtracerdirect PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Next-event estimation on the floor scene: two point lights sum their own
# terms, and ambientlight lights nothing, with a warning naming it and the
# count skipped. Linked against gman_pathtracer_objects, which still
# compiles GMANPathtraceRenderer itself. See
# tests/pathtracerdirectsum_test.cpp.
add_executable(gman_pathtracerdirectsum pathtracerdirectsum_test.cpp)
target_link_libraries(gman_pathtracerdirectsum PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerdirectsum gman_matte)

add_test(NAME pathtracerdirectsum COMMAND gman_pathtracerdirectsum)
set_tests_properties(pathtracerdirectsum PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The path tracer's own shadow walk: a two-face glass pane passes
# (1 - F)^2, a half-opaque matte blocker passes Os, and an opaque blocker
# passes nothing. Linked against gman_pathtracer_objects, which still
# compiles GMANPathtraceRenderer itself. See tests/pathtracershadow_test.cpp.
add_executable(gman_pathtracershadow pathtracershadow_test.cpp)
target_link_libraries(gman_pathtracershadow PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracershadow gman_matte gman_glass)

add_test(NAME pathtracershadow COMMAND gman_pathtracershadow)
set_tests_properties(pathtracershadow PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The shadow walk's own composite-layer cap, built twice from the same
# source at PATHTRACER_SHADOW_CAP_COUNT 0 and 1, each an offset from
# gman::kMaxCompositeLayers: at the cap, zero-opacity blockers stacked 0.1
# apart still pass light, and one more -- past gman::kMaxCompositeLayers --
# reads opaque. See tests/pathtracershadowcap_test.cpp.
add_executable(gman_pathtracershadowcap16 pathtracershadowcap_test.cpp)
target_compile_definitions(gman_pathtracershadowcap16 PRIVATE PATHTRACER_SHADOW_CAP_COUNT=0)
target_link_libraries(gman_pathtracershadowcap16 PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracershadowcap16 gman_matte)

add_test(NAME pathtracershadowcap16 COMMAND gman_pathtracershadowcap16)
set_tests_properties(pathtracershadowcap16 PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

add_executable(gman_pathtracershadowcap17 pathtracershadowcap_test.cpp)
target_compile_definitions(gman_pathtracershadowcap17 PRIVATE PATHTRACER_SHADOW_CAP_COUNT=1)
target_link_libraries(gman_pathtracershadowcap17 PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracershadowcap17 gman_matte)

add_test(NAME pathtracershadowcap17 COMMAND gman_pathtracershadowcap17)
set_tests_properties(pathtracershadowcap17 PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The sample buffer's own frame-level invariants: escape to background,
# the environment term under no light, coverage weighting colour and
# alpha, depth, antialiasing, a repeat and a crop matching bit for bit, an
# unresolvable indirect pass changing nothing but the log, and a
# non-finite escape dropping its own path. Linked against
# gman_pathtracer_objects, which still compiles GMANPathtraceRenderer
# itself. See tests/pathtracerframe_test.cpp.
add_executable(gman_pathtracerframe pathtracerframe_test.cpp)
target_link_libraries(gman_pathtracerframe PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerframe gman_matte)

add_test(NAME pathtracerframe COMMAND gman_pathtracerframe)
set_tests_properties(pathtracerframe PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# `gman -r gmanpathtracer` renders tests/rib/pathtracer_samples.rib to a
# non-black centre pixel and a black corner, and a second render writes the
# same pixels. Registered only when the plugin builds, since
# `-r gmanpathtracer` has nothing to load otherwise.
if(GMAN_BUILD_PATHTRACER)
  add_executable(gman_pathtracerrender pathtracerrender_test.cpp)
  target_link_libraries(gman_pathtracerrender PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_pathtracerrender gman_pathtracer)

  set(GMAN_PATHTRACERRENDER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/pathtracerrender-run")
  file(MAKE_DIRECTORY "${GMAN_PATHTRACERRENDER_WORKDIR}")

  add_test(NAME pathtracerrender
    COMMAND gman_pathtracerrender $<TARGET_FILE:gman_cli>
                                  "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_PATHTRACERRENDER_WORKDIR}")
  set_tests_properties(pathtracerrender PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# `gman -r gmanpathtracer` weights light and BSDF samples by a mapped card's bent normal.
if(GMAN_BUILD_PATHTRACER)
  add_executable(gman_normalmappathtracer normalmappathtracer_test.cpp)
  target_link_libraries(gman_normalmappathtracer PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_normalmappathtracer gman_pathtracer gman_paintedplastic)

  set(GMAN_NORMALMAPPATHTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/normalmappathtracer-run")
  file(MAKE_DIRECTORY "${GMAN_NORMALMAPPATHTRACER_WORKDIR}")

  add_test(NAME normalmappathtracer
    COMMAND gman_normalmappathtracer $<TARGET_FILE:gman_cli>
                                     "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_NORMALMAPPATHTRACER_WORKDIR}")
  set_tests_properties(normalmappathtracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 120)
endif()

# AreaLightSource parses through GMANRIBParse and builds a GMAN_LIGHT_AREA
# light; GMANAttributes' own pending area light scopes through
# Illuminate/AttributeBegin/AttributeEnd like the light list already does.
# Drives GMANRIBParse and GMANRenderManImpl directly, in process, following
# tests/pointscount_test.cpp's own technique, hence the same
# LD_LIBRARY_PATH/DYLD_LIBRARY_PATH as tests/basicstate.c. See
# tests/arealightsource_test.cpp.
add_executable(gman_arealightsource arealightsource_test.cpp)
target_link_libraries(gman_arealightsource PRIVATE gman gman_internal_headers)

add_test(NAME arealightsource
  COMMAND gman_arealightsource "${CMAKE_CURRENT_SOURCE_DIR}/rib")
set_tests_properties(arealightsource PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The counts-aware RiPolygonV rejects a negative or overflowing nverts,
# with or without a "P" parameter, warning once and adding nothing to the
# world rather than throwing or dropping the request silently. Drives
# GMANRenderManImpl directly, in process, the same technique and
# LD_LIBRARY_PATH/DYLD_LIBRARY_PATH as tests/arealightsource_test.cpp. See
# tests/polygonnverts_test.cpp.
add_executable(gman_polygonnverts polygonnverts_test.cpp)
target_link_libraries(gman_polygonnverts PRIVATE gman gman_internal_headers)

add_test(NAME polygonnverts COMMAND gman_polygonnverts)
set_tests_properties(polygonnverts PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# A world owns the primitives it is given: a GMANWorld destroys
# each one with itself, and an empty gman::Owned adds nothing. Each factory
# returns an empty owner for a mesh with no face, and a failed request adds
# nothing to the world, which an in-process ray-traced render with a radiosity
# pass shows by skipping no primitive. A NuPatch the ray factory drops warns
# once per world. See tests/primitiveowners_test.cpp.
add_executable(gman_primitiveowners primitiveowners_test.cpp)
target_link_libraries(gman_primitiveowners PRIVATE gman gman_ray gman_raster gman_internal_headers)

add_test(NAME primitiveowners COMMAND gman_primitiveowners)
set_tests_properties(primitiveowners PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

add_executable(gman_world world_test.cpp)
target_link_libraries(gman_world PRIVATE gman gman_internal_headers)

add_test(NAME world COMMAND gman_world)
set_tests_properties(world PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

gman_add_plugin(gman_worldprobe worldprobe_renderer_plugin.cpp)
target_link_libraries(gman_worldprobe PRIVATE gman gman_ray)
set_target_properties(gman_worldprobe PROPERTIES OUTPUT_NAME "worldprobe")

add_executable(gman_worldlifecycle worldlifecycle_test.cpp)
target_link_libraries(gman_worldlifecycle PRIVATE gman gman_internal_headers)
add_dependencies(gman_worldlifecycle gman_worldprobe)

add_test(NAME worldlifecycle COMMAND gman_worldlifecycle)
set_tests_properties(worldlifecycle PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# GMANSphere's and GMANDisk's own uniform-area sampler: a closed-form area,
# an independent finite-difference quadrature of the Jacobian, a
# uniform-by-area histogram, a degenerate primitive and the sampled normal.
# See tests/emitterprimitive_test.cpp.
add_executable(gman_emitterprimitive emitterprimitive_test.cpp)
target_link_libraries(gman_emitterprimitive PRIVATE gman gman_ray)

add_test(NAME emitterprimitive COMMAND gman_emitterprimitive)
set_tests_properties(emitterprimitive PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::emitters/sample/samplePoint over a hand-built world: enumeration,
# power(), sample's delta and area branches and samplePoint's delta branch,
# plus appearanceOf's own exclusion of an illuminated area light from
# Appearance::lights. See tests/emitter_test.cpp.
add_executable(gman_emitter emitter_test.cpp)
target_link_libraries(gman_emitter PRIVATE gman gman_ray)

add_test(NAME emitter COMMAND gman_emitter)
set_tests_properties(emitter PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gman::sample draws an area emitter uniformly in the solid angle it subtends:
# from hand-built receiving points, the pdf integrates to the share of draws
# accepted, every draw's pdf equals lightSolidAnglePdf at the hit its ray
# meets, and each region of the emitter takes its integral's share of the
# draws. See tests/emittersolidangle_test.cpp.
add_executable(gman_emittersolidangle emittersolidangle_test.cpp)
target_link_libraries(gman_emittersolidangle PRIVATE gman gman_ray)

add_test(NAME emittersolidangle COMMAND gman_emittersolidangle)
set_tests_properties(emittersolidangle PROPERTIES
  LABELS unit
  TIMEOUT 30)

# The path tracer's own area-light behaviour, split three ways so no one
# binary crowds the sanitizer leg's own time budget: a sphere light over the
# shared floor scene against its closed-form irradiance and a smoke check
# that droppedPathCount and alpha still hold on that render; the same floor
# on the far side of the sphere's own fixed preview point; and emitter-hit
# accounting covering a direct camera hit, a mirror bounce and a glass
# transmission. Each links against gman_pathtracer_objects, which still
# compiles GMANPathtraceRenderer itself. See tests/pathtracerarealight_test.cpp,
# tests/pathtracerarealightfarside_test.cpp and
# tests/pathtracerarealighthit_test.cpp.
add_executable(gman_pathtracerarealight pathtracerarealight_test.cpp)
target_link_libraries(gman_pathtracerarealight PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerarealight gman_matte)

add_test(NAME pathtracerarealight COMMAND gman_pathtracerarealight)
set_tests_properties(pathtracerarealight PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

add_executable(gman_pathtracerarealightfarside pathtracerarealightfarside_test.cpp)
target_link_libraries(gman_pathtracerarealightfarside PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerarealightfarside gman_matte)

add_test(NAME pathtracerarealightfarside COMMAND gman_pathtracerarealightfarside)
set_tests_properties(pathtracerarealightfarside PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

add_executable(gman_pathtracerarealighthit pathtracerarealighthit_test.cpp)
target_link_libraries(gman_pathtracerarealighthit PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerarealighthit gman_mirror gman_glass)

add_test(NAME pathtracerarealighthit COMMAND gman_pathtracerarealighthit)
set_tests_properties(pathtracerarealighthit PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The sphere-over-floor scene scaled about the eye, small and large, and
# with a translucent pane between the sphere and the floor: proofs that the
# shadow walk's own margin off the emitter tracks the scene's own scale and
# re-aims after a large blocker's own offset. See
# tests/pathtracerarealightscale_test.cpp,
# tests/pathtracerarealightscalelarge_test.cpp and
# tests/pathtracerarealightpane_test.cpp.
add_executable(gman_pathtracerarealightscale pathtracerarealightscale_test.cpp)
target_link_libraries(gman_pathtracerarealightscale PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerarealightscale gman_matte)

add_test(NAME pathtracerarealightscale COMMAND gman_pathtracerarealightscale)
set_tests_properties(pathtracerarealightscale PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

add_executable(gman_pathtracerarealightscalelarge pathtracerarealightscalelarge_test.cpp)
target_link_libraries(gman_pathtracerarealightscalelarge PRIVATE gman gman_internal_headers
  gman_pathtracer_objects)
add_dependencies(gman_pathtracerarealightscalelarge gman_matte)

add_test(NAME pathtracerarealightscalelarge COMMAND gman_pathtracerarealightscalelarge)
set_tests_properties(pathtracerarealightscalelarge PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

add_executable(gman_pathtracerarealightpane pathtracerarealightpane_test.cpp)
target_link_libraries(gman_pathtracerarealightpane PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerarealightpane gman_matte)

add_test(NAME pathtracerarealightpane COMMAND gman_pathtracerarealightpane)
set_tests_properties(pathtracerarealightpane PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# A polygon panel under the path tracer: the floor under a 2 x 2 panel against
# Lambert's formula for a polygonal luminaire, and a camera ray's direct view
# of panels wound toward and away from it, one with vertex normals that
# disagree with its plane normal. See tests/pathtracerpolygonlight_test.cpp.
add_executable(gman_pathtracerpolygonlight pathtracerpolygonlight_test.cpp)
target_link_libraries(gman_pathtracerpolygonlight PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerpolygonlight gman_matte)

add_test(NAME pathtracerpolygonlight COMMAND gman_pathtracerpolygonlight)
set_tests_properties(pathtracerpolygonlight PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The renderer's own multiple-importance-sampling weight computation,
# called directly rather than through a render: next-event estimation's
# weight, an emitter hit's weight, the light-choice probability the two
# share and the off switch. Linked against gman_pathtracer_objects, which
# still compiles GMANPathtraceRenderer itself, the same reachability
# tests/hitbsdf_test.cpp has into libgman via gman_internal_headers. See
# tests/pathtracermisweight_test.cpp.
add_executable(gman_pathtracermisweight pathtracermisweight_test.cpp)
target_link_libraries(gman_pathtracermisweight PRIVATE gman gman_internal_headers gman_pathtracer_objects)

add_test(NAME pathtracermisweight COMMAND gman_pathtracermisweight)
set_tests_properties(pathtracermisweight PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Veach's plates: a sharp GGX lobe under a large area light. MIS-on and
# light-only sampling must converge to the same high-sample reference
# mean, and MIS-on's own residual variance must fall well below
# light-only's -- the physically meaningful claim multiple importance
# sampling exists to deliver. See tests/pathtracermisveach_test.cpp.
add_executable(gman_pathtracermisveach pathtracermisveach_test.cpp)
target_link_libraries(gman_pathtracermisveach PRIVATE gman gman_internal_headers gman_pathtracer_objects)

add_test(NAME pathtracermisveach COMMAND gman_pathtracermisveach)
set_tests_properties(pathtracermisveach PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Two area lights of different power and distance over one floor: the
# floor's own mean sums their analytic irradiance, guarding the one
# mistake no single-light area-light scene here can catch -- the
# light-choice probability read at the wrong vertex. See
# tests/pathtracermistwolight_test.cpp.
add_executable(gman_pathtracermistwolight pathtracermistwolight_test.cpp)
target_link_libraries(gman_pathtracermistwolight PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracermistwolight gman_matte)

add_test(NAME pathtracermistwolight COMMAND gman_pathtracermistwolight)
set_tests_properties(pathtracermistwolight PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Light choice at a point on an emitter excludes that emitter. A camera-facing
# emitting disk under one distant light: the disk's own light-choice weight
# peaks on its own surface, so without the exclusion next-event estimation
# there wastes almost every draw and divides the rare survivor by a tiny
# probability. The same disk, matte, beside a second emitting sphere: a BSDF
# draw from the disk that lands on the sphere weighs itself against a
# light-choice probability that excludes the disk, so the MIS-on and MIS-off
# renders agree. See tests/pathtracerselfexclusion_test.cpp.
add_executable(gman_pathtracerselfexclusion pathtracerselfexclusion_test.cpp)
target_link_libraries(gman_pathtracerselfexclusion PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerselfexclusion gman_matte)

add_test(NAME pathtracerselfexclusion COMMAND gman_pathtracerselfexclusion)
set_tests_properties(pathtracerselfexclusion PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# An area light seen through a glass pane counts once: next-event
# estimation's own shadow ray already carries it through the dielectric's
# shadowTransmittance, so a refracting BSDF-sampled bounce that lands on
# the same light must not add it again. See
# tests/pathtracerglasscount_test.cpp.
add_executable(gman_pathtracerglasscount pathtracerglasscount_test.cpp)
target_link_libraries(gman_pathtracerglasscount PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerglasscount gman_matte gman_glass)

add_test(NAME pathtracerglasscount COMMAND gman_pathtracerglasscount)
set_tests_properties(pathtracerglasscount PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The path tracer renders its rows through gman::parallelFor, one texture
# cache per worker: an image byte-identical at one and at eight workers,
# the worker each row ran on, an equal dropped-path count at one, three and
# eight workers, one missing-texture warning however many workers ran, and a
# viewing system's camera calls through a const reference. The argument
# --scaling times the floor scene at one and at eight workers and asserts
# nothing. See tests/pathtracerthreads_test.cpp.
add_executable(gman_pathtracerthreads pathtracerthreads_test.cpp)
target_link_libraries(gman_pathtracerthreads PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerthreads gman_matte gman_paintedplastic)

add_test(NAME pathtracerthreads COMMAND gman_pathtracerthreads)
set_tests_properties(pathtracerthreads PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The path tracer samples a slot in two stages under a positive
# PixelVariance: a pilot of minsamples paths sizes a fresh batch, the slot's
# whole estimate. See tests/pathtraceradaptive_test.cpp. The argument
# --measure times samples/materials.rib under several path counts and
# PixelVariance values and asserts nothing.
add_executable(gman_pathtraceradaptive pathtraceradaptive_test.cpp)
target_link_libraries(gman_pathtraceradaptive PRIVATE gman gman_internal_headers gman_pathtracer_objects
                      TIFF::TIFF)
add_dependencies(gman_pathtraceradaptive gman_matte)

add_test(NAME pathtraceradaptive COMMAND gman_pathtraceradaptive)
set_tests_properties(pathtraceradaptive PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The path tracer's a-trous denoise filter on synthetic images: its noise
# reduction, edge preservation, kernel footprint, fixed point, non-finite
# exclusion and argument guards. See tests/denoise_test.cpp.
add_executable(gman_denoise denoise_test.cpp)
target_link_libraries(gman_denoise PRIVATE gman gman_internal_headers gman_pathtracer_objects)

add_test(NAME denoise COMMAND gman_denoise)
set_tests_properties(denoise PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The path tracer's denoise stage: Option "pathtracer" "denoise" off by
# default, the first-hit albedo and normal guides, the info line, the
# parameter warnings and the worker count's bit-for-bit invariance. See
# tests/pathtracerdenoise_test.cpp.
add_executable(gman_pathtracerdenoise pathtracerdenoise_test.cpp)
target_link_libraries(gman_pathtracerdenoise PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerdenoise gman_matte)

add_test(NAME pathtracerdenoise COMMAND gman_pathtracerdenoise)
set_tests_properties(pathtracerdenoise PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# A slot the path tracer stops under a positive PixelVariance averages to
# the fixed-count mean, shown on a hairline torus whose paths rarely hit it,
# and a slot's depth follows its fresh batch's coverage. See
# tests/pathtracerunbiased_test.cpp.
add_executable(gman_pathtracerunbiased pathtracerunbiased_test.cpp)
target_link_libraries(gman_pathtracerunbiased PRIVATE gman gman_internal_headers gman_pathtracer_objects)
add_dependencies(gman_pathtracerunbiased gman_matte)

add_test(NAME pathtracerunbiased COMMAND gman_pathtracerunbiased)
set_tests_properties(pathtracerunbiased PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# The z-buffer renders tests/rib/arealight_sphere.rib pixel for pixel as it
# would with every AreaLightSource request stripped out, without calling
# gman::emitters or special-casing an AreaLightSource-tagged primitive's own
# shading, and warns once naming the one area-light primitive it found. The
# ray tracer lights from the area light: its render of the same fixture logs
# no "has no effect here" and differs from the stripped scene's. `gman -r` runs
# each fixture in its own process: both loadable renderers keep their own object
# manager in one process-lifetime static instance, so two parses sharing a
# process would carry the first one's own tally into the second. LABELS
# render, since it runs gman itself rather than linking against libgman
# directly. Registered twice, one add_test per renderer flag, as
# tests/worldnodisplay_test.cpp's own gman_worldnodisplay is. See
# tests/rendererarealightwarning_test.cpp.
add_executable(gman_rendererarealightwarning rendererarealightwarning_test.cpp)
target_link_libraries(gman_rendererarealightwarning PRIVATE gman TIFF::TIFF)
add_dependencies(gman_rendererarealightwarning gman_zbuffer gman_matte)

set(GMAN_RENDERERAREALIGHTWARNING_ZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/rendererarealightwarning-zbuffer-run")
file(MAKE_DIRECTORY "${GMAN_RENDERERAREALIGHTWARNING_ZBUFFER_WORKDIR}")

add_test(NAME rendererarealightwarning_zbuffer
  COMMAND gman_rendererarealightwarning $<TARGET_FILE:gman_cli> gmanzbuffer "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_RENDERERAREALIGHTWARNING_ZBUFFER_WORKDIR}")
set_tests_properties(rendererarealightwarning_zbuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_rendererarealightwarning gman_raytracer)

  set(GMAN_RENDERERAREALIGHTWARNING_RAYTRACER_WORKDIR
    "${CMAKE_CURRENT_BINARY_DIR}/rendererarealightwarning-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_RENDERERAREALIGHTWARNING_RAYTRACER_WORKDIR}")

  add_test(NAME rendererarealightwarning_raytracer
    COMMAND gman_rendererarealightwarning $<TARGET_FILE:gman_cli> gmanraytracer "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_RENDERERAREALIGHTWARNING_RAYTRACER_WORKDIR}")
  set_tests_properties(rendererarealightwarning_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# tests/rib/arealight_polygon.rib tags two polygons with one
# AreaLightSource: one with a "P", one with only a "Cs". The z-buffer's
# area-light count names only the one that reaches the object manager. Its
# own working directory, as rendererarealightwarning_zbuffer's is. See
# tests/arealightpolygon_test.cpp.
add_executable(gman_arealightpolygon arealightpolygon_test.cpp)
target_link_libraries(gman_arealightpolygon PRIVATE gman TIFF::TIFF)
add_dependencies(gman_arealightpolygon gman_zbuffer gman_matte)

set(GMAN_AREALIGHTPOLYGON_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/arealightpolygon-run")
file(MAKE_DIRECTORY "${GMAN_AREALIGHTPOLYGON_WORKDIR}")

add_test(NAME arealightpolygon
  COMMAND gman_arealightpolygon $<TARGET_FILE:gman_cli> "${CMAKE_CURRENT_SOURCE_DIR}/rib"
  WORKING_DIRECTORY "${GMAN_AREALIGHTPOLYGON_WORKDIR}")
set_tests_properties(arealightpolygon PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

# tests/rib/arealight_panel.rib tags a Polygon wound to light the camera and one
# wound away with one AreaLightSource: under gmanpathtracer and under
# gmanraytracer the first reads the light's intensity times its colour and the
# second reads black. One add_test per renderer flag, each in its own working
# directory, as arealightpolygon's is. See tests/arealightpanel_test.cpp.
if(GMAN_BUILD_PATHTRACER OR GMAN_BUILD_RAYTRACER)
  add_executable(gman_arealightpanel arealightpanel_test.cpp)
  target_link_libraries(gman_arealightpanel PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_arealightpanel gman_matte)
endif()

if(GMAN_BUILD_PATHTRACER)
  add_dependencies(gman_arealightpanel gman_pathtracer)

  set(GMAN_AREALIGHTPANEL_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/arealightpanel-run")
  file(MAKE_DIRECTORY "${GMAN_AREALIGHTPANEL_WORKDIR}")

  add_test(NAME arealightpanel
    COMMAND gman_arealightpanel $<TARGET_FILE:gman_cli> gmanpathtracer "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_AREALIGHTPANEL_WORKDIR}")
  set_tests_properties(arealightpanel PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_arealightpanel gman_raytracer)

  set(GMAN_AREALIGHTPANEL_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/arealightpanel-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_AREALIGHTPANEL_RAYTRACER_WORKDIR}")

  add_test(NAME arealightpanel_raytracer
    COMMAND gman_arealightpanel $<TARGET_FILE:gman_cli> gmanraytracer "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_AREALIGHTPANEL_RAYTRACER_WORKDIR}")
  set_tests_properties(arealightpanel_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# A surface shader's BSDF: the Lambert-of-Cs default, albedo answered from
# rhoD, matte's Lambert lobe of Kd * Cs, the GGX closures of plastic,
# paintedplastic, metal and shinymetal, and mirror's and glass's delta
# closures, with every shader loaded through GMANAttributes::setSurface.
# add_dependencies below lists every plugin it loads, countingshader
# included: the loaded-default case needs it, since every shipped plugin
# overrides bsdf. Its own working directory, since the paintedplastic
# case writes and reads a texture file relative to cwd.
add_executable(gman_shaderbsdf shaderbsdf_test.cpp)
target_link_libraries(gman_shaderbsdf PRIVATE gman TIFF::TIFF)
add_dependencies(gman_shaderbsdf gman_matte gman_plastic gman_paintedplastic gman_metal gman_shinymetal gman_mirror
  gman_glass gman_countingshader)

set(GMAN_SHADERBSDF_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/shaderbsdf-run")
file(MAKE_DIRECTORY "${GMAN_SHADERBSDF_WORKDIR}")

add_test(NAME shaderbsdf COMMAND gman_shaderbsdf
  WORKING_DIRECTORY "${GMAN_SHADERBSDF_WORKDIR}")
set_tests_properties(shaderbsdf PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS unit
  TIMEOUT 30)

# Checks one image the install test's renders write: it opens as a TIFF of the
# width and height given and, with a fourth argument `lit`, carries a pixel
# with a nonzero colour channel. See tests/installimage_test.cpp.
add_executable(gman_installimage installimage_test.cpp)
target_link_libraries(gman_installimage PRIVATE TIFF::TIFF)

# Proves both halves of an installed gman. The headers the install writes
# directly under include/gman are exactly the ones tests/installed_headers.txt
# lists, so a header that appears or disappears fails the test by name. The
# prefix holds exactly one libgman.* file, the core library, so that library
# cannot install under another name. bin/gman renders every shipped shader
# (matte, plastic, metal, paintedplastic, shinymetal) from a fresh cmake
# --install prefix, then the ray tracer, the path tracer and the ray tracer's
# radiosity pass, each from the plugin that loads it by bare name.
# gman_installimage checks every one of those images at the size its fixture
# gives. tests/rib/smoke.rib then renders under every installed renderer, each
# image the size of its Format and carrying a lit pixel, and a guard fails when
# the prefix installs a renderer no row renders. An unrelated CMake project
# then finds the prefix with find_package(gman) and builds four programs: one
# links gman::gman alone and compiles <gman/ri.h>, one links gman::gman_ray
# alone and calls into libgman_ray, one links gman::gman_raster alone and calls
# into libgman_raster, and one links all three, renders a lit sphere and calls
# into each library; the include path and core link of the ray and raster
# programs reach them through the library's public dependency. It also builds,
# without loading, a shader plugin against libgman and a renderer plugin
# against libgman and libgman_ray. The parent's CMAKE_MODULE_LINKER_FLAGS reach
# that project, so its plugins link as the parent's own do. See
# tests/install_test.cmake for the steps and tests/install/ for the consumer
# project it builds.
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
          -D "GMAN_MODULE_LINKER_FLAGS=${CMAKE_MODULE_LINKER_FLAGS}"
          -D "GMAN_HAS_RAYTRACER=${GMAN_BUILD_RAYTRACER}"
          -D "GMAN_HAS_PATHTRACER=${GMAN_BUILD_PATHTRACER}"
          -D "GMAN_HAS_RADIOSITY=${GMAN_BUILD_RADIOSITY}"
          -D "GMAN_INSTALLED_HEADERS_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/installed_headers.txt"
          -D "GMAN_IMAGECHECK=$<TARGET_FILE:gman_installimage>"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/install_test.cmake")
set_tests_properties(install PROPERTIES
  LABELS render
  TIMEOUT 300
  RESOURCE_LOCK gmanrib_plugin_file)

# Proves packaging/install.sh, the script a release tarball ships. It stages
# a tarball tree as release.yml does, installs it beside an unrelated file,
# renders samples/vase.rib with the installed gman, installs again, uninstalls
# and refuses a bad flag. See tests/installscript_test.cmake.
#
# The staging cmake --install reads libgmanrib.so from the build tree, so this
# carries the same RESOURCE_LOCK as install.
add_test(NAME installscript
  COMMAND "${CMAKE_COMMAND}"
          -D "GMAN_BUILD_DIR=${CMAKE_BINARY_DIR}"
          -D "GMAN_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
          -D "GMAN_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/installscript-run"
          -D "GMAN_HAS_RAYTRACER=${GMAN_BUILD_RAYTRACER}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/installscript_test.cmake")
set_tests_properties(installscript PROPERTIES
  LABELS render
  TIMEOUT 300
  RESOURCE_LOCK gmanrib_plugin_file)

# runGman's contract: exit status, the three capture modes, a signal kill,
# a timeout bounding both read and wait, a working directory, a redirected
# stdin and an exec failure, each leaving no child behind. See
# tests/rungman_test.cpp.
add_executable(gman_rungman rungman_test.cpp)
add_test(NAME rungman COMMAND gman_rungman)
set_tests_properties(rungman PROPERTIES
  LABELS unit
  TIMEOUT 30)

# Fails on a hand-rolled process spawn outside tests/rungman.h or a
# transform built directly from one matrix outside
# tests/maketransform.h, anywhere under tests/. See
# tests/helperscan_test.cpp.
add_executable(gman_helperscan helperscan_test.cpp)
add_test(NAME helperscan COMMAND gman_helperscan "${CMAKE_SOURCE_DIR}/tests")
set_tests_properties(helperscan PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANMin, GMANMax and GMANClamp drop a NaN operand for float and double, as
# fmin and fmax do, and follow the standard library for every other type. See
# tests/gmanmathminmax_test.cpp.
add_executable(gman_gmanmathminmax gmanmathminmax_test.cpp)
target_link_libraries(gman_gmanmathminmax PRIVATE gman)

add_test(NAME gmanmathminmax COMMAND gman_gmanmathminmax)
set_tests_properties(gmanmathminmax PROPERTIES
  LABELS unit
  TIMEOUT 30)

# gmanmath.h's shading language functions -- radians, degrees, atan,
# inversesqrt, log, mod, sign, mix and round -- meet their definitions and
# IEEE 754 edges. See tests/gmanmath_test.cpp.
add_executable(gman_gmanmath gmanmath_test.cpp)
target_link_libraries(gman_gmanmath PRIVATE gman)

add_test(NAME gmanmath COMMAND gman_gmanmath)
set_tests_properties(gmanmath PROPERTIES
  LABELS unit
  TIMEOUT 30)

# GMANMod(-4, 2) answers 2: a negative multiple of b is congruent to 0
# under RSL mod(a, b) = a - b * floor(a / b). Pins the wanted behavior;
# WILL_FAIL until GMANMod follows the definition for a negative dividend.
add_executable(gman_gmanmathmodmultiple gmanmathmodmultiple_test.cpp)
target_link_libraries(gman_gmanmathmodmultiple PRIVATE gman)

add_test(NAME gmanmathmodmultiple COMMAND gman_gmanmathmodmultiple)
set_tests_properties(gmanmathmodmultiple PROPERTIES
  WILL_FAIL TRUE
  LABELS unit
  TIMEOUT 30)

# GMANMod(5, -3) and GMANMod(-5, -3) answer 2 and -5 where RSL mod(a, b) =
# a - b * floor(a / b) gives -1 and -2: a negative divisor is treated as
# positive. Pins the wanted behavior; WILL_FAIL until GMANMod follows the
# definition for a negative divisor.
add_executable(gman_gmanmathmodnegativedivisor gmanmathmodnegativedivisor_test.cpp)
target_link_libraries(gman_gmanmathmodnegativedivisor PRIVATE gman)

add_test(NAME gmanmathmodnegativedivisor COMMAND gman_gmanmathmodnegativedivisor)
set_tests_properties(gmanmathmodnegativedivisor PROPERTIES
  WILL_FAIL TRUE
  LABELS unit
  TIMEOUT 30)

# GMANInversesqrt answers RI_INFINITY for every x whose square root is at
# most RI_EPSILON, and for NaN, where IEEE 754 gives 1 / sqrt(x): 2e10 at
# RI_EPSILON^2 / 4, +infinity at +0, -infinity at -0 and NaN below 0 or for NaN.
# Pins the wanted behavior; WILL_FAIL until GMANInversesqrt follows IEEE there.
add_executable(gman_gmanmathinversesqrtclamp gmanmathinversesqrtclamp_test.cpp)
target_link_libraries(gman_gmanmathinversesqrtclamp PRIVATE gman)

add_test(NAME gmanmathinversesqrtclamp COMMAND gman_gmanmathinversesqrtclamp)
set_tests_properties(gmanmathinversesqrtclamp PROPERTIES
  WILL_FAIL TRUE
  LABELS unit
  TIMEOUT 30)

# GMANSign(NaN) answers 0 where IEEE 754 gives NaN for a NaN operand. Pins
# the wanted behavior; WILL_FAIL until GMANSign returns NaN for NaN.
add_executable(gman_gmanmathsignnan gmanmathsignnan_test.cpp)
target_link_libraries(gman_gmanmathsignnan PRIVATE gman)

add_test(NAME gmanmathsignnan COMMAND gman_gmanmathsignnan)
set_tests_properties(gmanmathsignnan PROPERTIES
  WILL_FAIL TRUE
  LABELS unit
  TIMEOUT 30)

# gman::InvertibleMatrix pairs a placement with its inverse; its transformNormal
# applies the inverse transpose. See tests/invertiblematrix_test.cpp.
add_executable(gman_invertiblematrix invertiblematrix_test.cpp)
target_link_libraries(gman_invertiblematrix PRIVATE gman)

add_test(NAME invertiblematrix COMMAND gman_invertiblematrix)
set_tests_properties(invertiblematrix PROPERTIES
  LABELS unit
  TIMEOUT 30)

# A request out of mode is reported and ignored; a block request out of place stops the file.
add_executable(gman_outofmode outofmode_test.cpp)
target_link_libraries(gman_outofmode PRIVATE gman gman_internal_headers TIFF::TIFF)
add_dependencies(gman_outofmode gman_zbuffer)

set(GMAN_OUTOFMODE_ZBUFFER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/outofmode-zbuffer-run")
file(MAKE_DIRECTORY "${GMAN_OUTOFMODE_ZBUFFER_WORKDIR}")

add_test(NAME outofmode_zbuffer
  COMMAND gman_outofmode $<TARGET_FILE:gman_cli>
                         "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                         gmanzbuffer
  WORKING_DIRECTORY "${GMAN_OUTOFMODE_ZBUFFER_WORKDIR}")
set_tests_properties(outofmode_zbuffer PROPERTIES
  ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
  LABELS render
  TIMEOUT 30)

if(GMAN_BUILD_RAYTRACER)
  add_dependencies(gman_outofmode gman_raytracer)

  set(GMAN_OUTOFMODE_RAYTRACER_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/outofmode-raytracer-run")
  file(MAKE_DIRECTORY "${GMAN_OUTOFMODE_RAYTRACER_WORKDIR}")

  add_test(NAME outofmode_raytracer
    COMMAND gman_outofmode $<TARGET_FILE:gman_cli>
                           "${CMAKE_CURRENT_SOURCE_DIR}/rib"
                           gmanraytracer
    WORKING_DIRECTORY "${GMAN_OUTOFMODE_RAYTRACER_WORKDIR}")
  set_tests_properties(outofmode_raytracer PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 30)
endif()

# glass's eta, Kr and Kt move the ray tracer and the path tracer alike: a pane,
# a sphere and a shadowed receiver render under both renderers, cropped to one
# block, beside twins that drop the reflected panel, the backdrop or the pane.
# Needs both renderers. See tests/glassparams_test.cpp.
if(GMAN_BUILD_RAYTRACER AND GMAN_BUILD_PATHTRACER)
  add_executable(gman_glassparams glassparams_test.cpp)
  target_link_libraries(gman_glassparams PRIVATE gman TIFF::TIFF)
  add_dependencies(gman_glassparams gman_raytracer gman_pathtracer gman_glass gman_matte)

  set(GMAN_GLASSPARAMS_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/glassparams-run")
  file(MAKE_DIRECTORY "${GMAN_GLASSPARAMS_WORKDIR}")

  add_test(NAME glassparams
    COMMAND gman_glassparams $<TARGET_FILE:gman_cli>
                             "${CMAKE_CURRENT_SOURCE_DIR}/rib"
    WORKING_DIRECTORY "${GMAN_GLASSPARAMS_WORKDIR}")
  set_tests_properties(glassparams PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR};DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}"
    LABELS render
    TIMEOUT 120)
endif()
