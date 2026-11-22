cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Harry Potter and the Chamber of Secrets (Xbox, NTSC-US) - Static Recompilation
#
# Host: macOS on Apple silicon (Metal NV2A renderer, SDL3 through sdl2-compat).
# Built on the NFSU2 Xbox port's engine (nfsu2-xbox-port); CMake option and
# runtime switch names keep its NFSU2_ prefix (they are the runtime's names).
# ============================================================================

project(hpcos_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/xboxrecomp" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# The lifted code is ~100 MB of C generated from the retail XBE. It is never
# committed; point this at wherever tools.recomp --gen-dir wrote it.
set(NFSU2_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp/gen" CACHE PATH
    "Directory holding the tools.recomp output (recomp_*.c, recomp_funcs.h)")

if(NOT EXISTS "${NFSU2_GEN_DIR}/recomp_funcs.h")
    message(FATAL_ERROR "No generated code in ${NFSU2_GEN_DIR}. Run tools.recomp "
                        "with --gen-dir, or pass -DNFSU2_GEN_DIR=<dir>.")
endif()

# The Vulkan renderer (xboxrecomp nv2a_vk) instead of OpenGL. On the Switch it
# links mesa-switch's static NVK (NVK_SDK) and builds nfsu2x-vulkan.nro.
option(NFSU2_VULKAN "Render with Vulkan instead of OpenGL" OFF)
set(XBOXRECOMP_VULKAN ${NFSU2_VULKAN} CACHE BOOL "" FORCE)
# Apple: the native Metal renderer too, the default at run time;
# NFSU2_RENDERER=vulkan picks MoltenVK instead (src/apple/apple_main.c).
if(APPLE)   # macOS: Metal alone (NFSU2_VULKAN=OFF) or beside MoltenVK
    option(NFSU2_METAL "Also build the Metal renderer" ON)
else()
    set(NFSU2_METAL OFF)
endif()
set(XBOXRECOMP_METAL ${NFSU2_METAL} CACHE BOOL "" FORCE)

add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${NFSU2_GEN_DIR}/*.c")

set(NFSU2_MANUAL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp_manual.c" CACHE FILEPATH
    "Manual overrides matching the translated executable")

set(GAME_SOURCES
    src/main.c
    ${NFSU2_MANUAL_SOURCE}
)

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${GAME_SOURCES} ${RECOMP_GEN_SOURCES})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)

# AppKit owns the main thread: window and events there, the title on its own
# thread (src/apple/apple_main.c).
if(APPLE)
    set_property(SOURCE ${NFSU2_MANUAL_SOURCE} APPEND PROPERTY COMPILE_OPTIONS "-ffp-contract=off")
    enable_language(OBJC)
    enable_language(CXX)
    enable_language(OBJCXX)
    target_sources(${PROJECT_NAME} PRIVATE src/apple/apple_main.c src/apple/apple_game_state.c src/apple/apple_perf.m
                                           src/apple/apple_keyboard.c src/apple/apple_window.m
                                           src/apple/apple_menu.m src/apple/apple_player.c)
    set_source_files_properties(src/apple/apple_window.m
                                src/apple/apple_menu.m
                                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(src/apple/apple_perf.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
                                INCLUDE_DIRECTORIES "${XBOXRECOMP_DIR}/src/apu")
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Foundation" "-framework CoreGraphics"
                          "-framework ImageIO" "-framework CoreText" "-framework AppKit")

    # The overlay menu (src/overlay): Dear ImGui 1.92.9 (third_party/imgui, MIT) on Metal, the Roboto
    # Medium font (third_party/fonts, Apache-2.0) compiled in, this port's version on its Advanced page.
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    add_library(imgui STATIC ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
                ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/backends/imgui_impl_metal.mm)
    target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    set_source_files_properties(${IMGUI_DIR}/backends/imgui_impl_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/Roboto-Medium.ttf OVERLAY_FONT_HEX HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," OVERLAY_FONT_BYTES "${OVERLAY_FONT_HEX}")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated/overlay_font.inc "${OVERLAY_FONT_BYTES}\n")
    set(PORT_VERSION "dev")
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION)
        file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION PORT_VERSION LIMIT_COUNT 1)
    endif()
    execute_process(COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --short HEAD
                    OUTPUT_VARIABLE PORT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT PORT_COMMIT)
        set(PORT_COMMIT "local")
    endif()
    target_sources(${PROJECT_NAME} PRIVATE src/overlay/overlay.mm src/overlay/hpcos_options.c)
    set_source_files_properties(src/overlay/overlay.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
                                INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set_source_files_properties(src/overlay/hpcos_options.c PROPERTIES COMPILE_DEFINITIONS
                                "NFSU2_PORT_VERSION=\"${PORT_VERSION}\";NFSU2_PORT_COMMIT=\"${PORT_COMMIT}\"")
    target_link_libraries(${PROJECT_NAME} PRIVATE imgui "-framework Metal" "-framework QuartzCore")
    target_link_libraries(imgui PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Foundation")
endif()

if(NFSU2_VULKAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_VULKAN=1)
endif()
if(NFSU2_METAL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NFSU2_METAL=1)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${NFSU2_GEN_DIR}
    ${XBOXRECOMP_DIR}/include
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d11 dxgi dxguid xinput winmm dbghelp)
    if(MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE /LARGEADDRESSAWARE)
        target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
    endif()
else()
    # The generated chunks are huge straight-line functions; -O1 keeps GCC's
    # memory and time per chunk sane while still allocating registers. The
    # Switch and Apple arm64 use -O2; override NFSU2_GEN_OPT to trade build
    # time and memory for optimisation on a particular machine.
    if(APPLE)
        set(NFSU2_GEN_OPT "-O2" CACHE STRING "Optimisation for the lifted code")
    else()
        set(NFSU2_GEN_OPT "-O1" CACHE STRING "Optimisation for the lifted code")
    endif()
    # NFSU2_LTO: link-time optimisation of the lifted code and its manual
    # overrides, so lifted callers inline each other and the native leaves
    # (_ftol2, ...). The runtime and renderers are compiled as usual.
    # NFSU2_LTO_JOBS parallel LTRANS jobs (memory: ~1-2 GB each).
    option(NFSU2_LTO "Link-time optimisation of the lifted code" OFF)
    set(NFSU2_LTO_JOBS "4" CACHE STRING "Parallel LTO link jobs")
    set(NFSU2_GEN_FLAGS "${NFSU2_GEN_OPT};-w;-fno-strict-aliasing;-fwrapv")
    if(NFSU2_LTO AND CMAKE_C_COMPILER_ID MATCHES "Clang")
        list(APPEND NFSU2_GEN_FLAGS "-flto=thin")
        set_property(SOURCE ${NFSU2_MANUAL_SOURCE} APPEND PROPERTY
            COMPILE_OPTIONS "-flto=thin;-fno-strict-aliasing;-fwrapv")
        target_link_options(${PROJECT_NAME} PRIVATE "-flto=thin" ${NFSU2_GEN_OPT})
    elseif(NFSU2_LTO)
        list(APPEND NFSU2_GEN_FLAGS "-flto=auto")
        set_property(SOURCE ${NFSU2_MANUAL_SOURCE} APPEND PROPERTY
            COMPILE_OPTIONS "-flto=auto;-fno-strict-aliasing;-fwrapv")
        target_link_options(${PROJECT_NAME} PRIVATE
            "-flto=${NFSU2_LTO_JOBS}" "-flto-partition=balanced" ${NFSU2_GEN_OPT})
    endif()
    set_source_files_properties(${RECOMP_GEN_SOURCES} PROPERTIES
        COMPILE_OPTIONS "${NFSU2_GEN_FLAGS}")
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()
