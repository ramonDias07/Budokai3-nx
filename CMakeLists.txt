cmake_minimum_required(VERSION 3.25)
project(Budokai3NX VERSION 0.1.40 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk" CACHE PATH "Patched ReXGlue SDK")
set(BUDOKAI_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/app/generated" CACHE PATH "Local PowerPC recompilation")
set(BUDOKAI_ICON "" CACHE FILEPATH "Optional 256x256 JPEG for the local NRO")
set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "Use prepared dependencies")
set(REXGLUE_SWITCH_DEKO3D ON CACHE BOOL "Deko3D backend" FORCE)
set(REXGLUE_USE_VULKAN OFF CACHE BOOL "Disable Vulkan" FORCE)

if(NOT EXISTS "${BUDOKAI_GENERATED_DIR}/sources.cmake")
    message(FATAL_ERROR "Run ReXGlue codegen with app/dbz3_manifest.toml first (see docs/build.md).")
endif()
add_subdirectory("${REXSDK_DIR}" "${CMAKE_BINARY_DIR}/rexglue")
include("${BUDOKAI_GENERATED_DIR}/sources.cmake")
set_source_files_properties(${GENERATED_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/app/src/gcc_codegen_compat.h")

add_executable(budokai3_nx
    app/src/main.cpp
    app/src/graphics.cpp
    app/src/battle_audio.cpp
    app/src/b3_usa_dispatch.cpp
    app/src/nfsmw_nativo_shaders.cpp
    app/src/nfsmw_shader_library.cpp
    ${GENERATED_SOURCES})
target_include_directories(budokai3_nx PRIVATE "${BUDOKAI_GENERATED_DIR}" "${REXSDK_DIR}/thirdparty/xxHash")
target_compile_options(budokai3_nx PRIVATE -fno-strict-aliasing -ffp-contract=off)
target_link_libraries(budokai3_nx PRIVATE rex::runtime rex::gpu-xenos)
rexglue_switch_configure_target(budokai3_nx GPU_PLUGINS xenos)
rexglue_switch_add_deko3d(budokai3_nx)
if(BUDOKAI_ICON)
    set(icon_args ICON "${BUDOKAI_ICON}")
endif()
rexglue_switch_add_nro(budokai3_nx NAME "Budokai 3 USA - Xenos Deko3D"
    AUTHOR "Budokai port / ReXGlue" VERSION "${PROJECT_VERSION}" ${icon_args})
