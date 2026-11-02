cmake_minimum_required(VERSION 3.20.0)

# Application devicetree overlay: mic power rail boot-on, PDM2 enable,
# Axon NPU node enable (see app.overlay).
set(DTS_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/app.overlay" CACHE FILEPATH "devicetree overlay" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiao_nrf54lm20b_edgeai_wakeword)

# ---- sdk-edge-ai v3 module: locate the registered external module ----------
# The module dir comes from the registered ZEPHYR_MODULES (visible in every
# CMake re-run) plus the XIAO_EDGE_AI_DIR environment override; the
# framework-bundled v2 module under _pio is skipped.
set(_ea_module_dir "")
set(_ea_module_dirs "")
if(DEFINED ENV{XIAO_EDGE_AI_DIR})
  list(APPEND _ea_module_dirs "$ENV{XIAO_EDGE_AI_DIR}")
endif()
if(ZEPHYR_MODULES)
  list(APPEND _ea_module_dirs ${ZEPHYR_MODULES})
endif()
foreach(_ea_dir IN LISTS _ea_module_dirs)
  if(NOT _ea_dir MATCHES "/_pio/" AND EXISTS "${_ea_dir}/zephyr/module.yml")
    set(_ea_module_dir "${_ea_dir}")
  endif()
endforeach()

# ---- sdk-edge-ai v3: generate version/commit headers at CONFIGURE time -----
# PlatformIO's Zephyr builder compiles the sources itself from the CMake file
# API instead of driving ninja, so the module's add_custom_command() rules
# (edge_ai_version.h / edge_ai_commit.h, needed by subsys/edge_ai_banner.c)
# never run and the banner fails to compile. Generate the same outputs here
# during configure; ninja's own rules remain declared and are harmless
# duplicates (identical content).
if(_ea_module_dir)
  set(_ea_gen_dir "${CMAKE_BINARY_DIR}/zephyr/include/generated")
  # edge_ai_version.h: same invocation the module's cmake/version.cmake uses
  # (Zephyr's canonical gen_version_h.cmake, VERSION_TYPE=EDGE_AI).
  execute_process(
    COMMAND ${CMAKE_COMMAND}
      -DZEPHYR_BASE=${ZEPHYR_BASE}
      -DOUT_FILE=${_ea_gen_dir}/edge_ai_version.h
      -DVERSION_TYPE=EDGE_AI
      -DVERSION_FILE=${_ea_module_dir}/VERSION
      -P ${ZEPHYR_BASE}/cmake/gen_version_h.cmake
    RESULT_VARIABLE _ea_version_rc)
  if(NOT _ea_version_rc EQUAL 0)
    message(FATAL_ERROR "mdk-compat: edge_ai_version.h generation failed (${_ea_version_rc})")
  endif()
  message(STATUS "edge-ai: generated ${_ea_gen_dir}/edge_ai_version.h")

  # edge_ai_commit.h: upstream (NCS sdk-nrf cmake/gen_commit_h.cmake + commit.h.in)
  # is not shipped with this framework (ZEPHYR_NRF_MODULE_DIR is empty, the
  # module's own rule points at a nonexistent script), so replicate its exact
  # output here: the 12-char git hash of the sdk-edge-ai checkout.
  execute_process(
    COMMAND git -C ${_ea_module_dir} rev-parse HEAD
    OUTPUT_VARIABLE _ea_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _ea_commit_rc)
  if(_ea_commit_rc EQUAL 0 AND NOT _ea_commit STREQUAL "")
    string(SUBSTRING "${_ea_commit}" 0 12 _ea_commit)
    set(_ea_commit_content
"#ifndef _EDGE_AI_COMMIT_H_
#define _EDGE_AI_COMMIT_H_
#define EDGE_AI_COMMIT ${_ea_commit}
#define EDGE_AI_COMMIT_STRING \"${_ea_commit}\"
#endif /* _EDGE_AI_COMMIT_H_ */
")
    if(EXISTS "${_ea_gen_dir}/edge_ai_commit.h")
      file(READ "${_ea_gen_dir}/edge_ai_commit.h" _ea_commit_old)
    else()
      set(_ea_commit_old "")
    endif()
    if(NOT _ea_commit_old STREQUAL _ea_commit_content)
      file(WRITE "${_ea_gen_dir}/edge_ai_commit.h" "${_ea_commit_content}")
    endif()
    message(STATUS "edge-ai: generated ${_ea_gen_dir}/edge_ai_commit.h (commit ${_ea_commit})")
  else()
    message(WARNING "edge-ai: git hash of ${_ea_module_dir} unavailable; "
                    "edge_ai_commit.h not regenerated")
  endif()
endif()

# ---- sdk-edge-ai v3 / framework MDK compatibility -------------------------
# The build defines both NRF54LM20A_XXAA (because SOC_NRF54LM20B selects
# SOC_COMPATIBLE_NRF54LM20A) and NRF54LM20B_XXAA, and <mdk/nrf.h> tests the
# LM20A variant first, so the Edge AI runtime's platform file — which needs
# the LM20B-only AXONS NPU definitions (NRF_AXONS_Type, AXONS_ENABLE_EN_*) —
# does not compile. Source-file properties cannot fix this from application
# scope (they do not reach files added by module CMakeLists), so instead swap
# that ONE file out of the zephyr target for a generated wrapper that selects
# the LM20B headers and then compiles the original, unmodified SDK source.
# Only that single translation unit changes; everything else in the build
# (errata handling, peripheral headers) is untouched.
if(_ea_module_dir)
  set(_ea_platform_file
    "${_ea_module_dir}/lib/axon/platform/src/zephyr/nrf_axon_platform_zephyr.c")
  if(EXISTS "${_ea_platform_file}")
    get_target_property(_zephyr_srcs zephyr SOURCES)
    if(_zephyr_srcs)
      set(_new_srcs "")
      set(_swapped FALSE)
      get_filename_component(_ea_real "${_ea_platform_file}" REALPATH)
      foreach(_s IN LISTS _zephyr_srcs)
        get_filename_component(_s_real "${_s}" REALPATH)
        if(_s_real STREQUAL _ea_real)
          set(_swapped TRUE)
        else()
          list(APPEND _new_srcs "${_s}")
        endif()
      endforeach()
      if(_swapped)
        set(EA_PLATFORM_FILE "${_ea_platform_file}")
        configure_file(
          "${CMAKE_CURRENT_SOURCE_DIR}/nrf_axon_platform_wrapper.c.in"
          "${CMAKE_CURRENT_BINARY_DIR}/nrf_axon_platform_wrapper.c" @ONLY)
        set_property(TARGET zephyr PROPERTY SOURCES ${_new_srcs})
        target_sources(zephyr PRIVATE
          "${CMAKE_CURRENT_BINARY_DIR}/nrf_axon_platform_wrapper.c")
        message(STATUS "mdk-compat: LM20B header wrapper for ${_ea_platform_file}")
      else()
        message(STATUS "mdk-compat: platform file not in zephyr sources, no-op")
      endif()
    endif()
  endif()
endif()

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/nrf_edgeai_generated
)

# The generated model header uses C11 static_assert; newlib only defines it
# with <assert.h> in scope, so force-include it before any source.
target_compile_options(app PRIVATE "-include" "assert.h")

target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/nrf_edgeai_generated/nrf_edgeai_user_model.c
)
