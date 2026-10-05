cmake_minimum_required(VERSION 3.16)

# Force Clang as the default compiler (Must be set BEFORE project())
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

project(ZeroCopyVideocall C)

# Set Default Build Type to Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build (Debug, Release, etc)." FORCE)
endif()

# ==============================================================================
# GLOBAL COMPILER SETTINGS
# ==============================================================================
# Force C99 standard strictly. Do not allow compiler extensions (e.g., gnu99) 
# to ensure portability across POSIX systems.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Generate compile_commands.json for LSP (clangd) integration.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# Route build artifacts into isolated subdirectories to keep the tree clean.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ==============================================================================
# EXTERNAL DEPENDENCIES
# ==============================================================================
# SDL3: Vendored submodule. 
# EXCLUDE_FROM_ALL prevents SDL's tests/examples from polluting our build targets.
add_subdirectory(src/vendored/SDL EXCLUDE_FROM_ALL)

# ==============================================================================
# SANITIZERS 
# ==============================================================================
option(USE_ASAN "Enable AddressSanitizer for debugging" ON)
add_library(asan_config INTERFACE)
if(USE_ASAN)
    target_compile_options(asan_config INTERFACE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(asan_config INTERFACE -fsanitize=address)
endif()
option(NDEBUG OFF)


# Hardware acceleration stack.
# We strictly require libva and libva-drm for zero-copy kernel memory mapping.
find_package(PkgConfig REQUIRED)
pkg_check_modules(VAAPI REQUIRED libva libva-drm)

# Workaround: Some legacy VA-API headers might rely on global include paths.
include_directories(${VAAPI_INCLUDE_DIRS})

# ==============================================================================
# INTERNAL MODULES (Static Libraries)
# ==============================================================================

# ------------------------------------------------------------------------------
# Module: common
# Base layer containing agnostic helpers (math, memory handling, bit-banging).
# ------------------------------------------------------------------------------
add_library(common STATIC 
    src/common/utils.c
    src/common/utils.h
)
target_include_directories(common PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(common PRIVATE asan_config)

# ------------------------------------------------------------------------------
# Module: vaapi_core
# Shared hardware abstraction layer. Handles VADisplay, contexts, and surfaces.
# ------------------------------------------------------------------------------
add_library(vaapi_core STATIC 
    src/hardware/vaapi_core/vaapi_decode.h # main header
    src/hardware/vaapi_core/vaapi_decode.c
)
target_include_directories(vaapi_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vaapi_core PRIVATE ${VAAPI_LIBRARIES} asan_config)

# ------------------------------------------------------------------------------
# Module: jpeg_decoder
# Stateless MJPEG decoder. It use vaapi_core and jpeg_parser
# Depends on vaapi_core for GPU offloading.
# ------------------------------------------------------------------------------
# Define the source files for the JPEG decoder once
set(JPEG_DECODER_SRCS
    src/hardware/decode/jpeg/jpeg_decoder.h # public header
    src/hardware/decode/jpeg/jpeg_decoder_internal.h
    src/hardware/decode/jpeg/std_huff_tbls.h
    src/hardware/decode/jpeg/jpeg_decoder.c

    src/hardware/decode/jpeg/jpeg_vaapi_manager.c
    src/hardware/decode/jpeg/jpeg_vaapi_manager.h

    src/hardware/decode/jpeg/jpeg_hardware.h
    src/hardware/decode/jpeg/jpeg_hardware.c
)

# ------------------------------------------------------------------------------
# Target: jpeg_decoder (Production Variant)
# Used by the main app and fuzzers. Compiled to guarantee zero-overhead logging.
# ------------------------------------------------------------------------------
add_library(jpeg_decoder STATIC ${JPEG_DECODER_SRCS})
target_include_directories(jpeg_decoder PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(jpeg_decoder PRIVATE vaapi_core asan_config)
# Force NDEBUG to strip standard asserts, and ensure DEBUG is NOT defined
target_compile_definitions(jpeg_decoder PRIVATE NDEBUG)

# ------------------------------------------------------------------------------
# Target: jpeg_decoder_debug (Debug Variant)
# Used exclusively by manual tests. Compiled to guarantee logging is enabled.
# ------------------------------------------------------------------------------
add_library(jpeg_decoder_debug STATIC ${JPEG_DECODER_SRCS})
target_include_directories(jpeg_decoder_debug PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(jpeg_decoder_debug PRIVATE vaapi_core asan_config)
# Explicitly undefine NDEBUG (-UNDEBUG) so logging and asserts are ALWAYS active,
# even if CMake is doing a global 'Release' build.
target_compile_options(jpeg_decoder_debug PRIVATE -UNDEBUG)

# ==============================================================================
# MAIN APPLICATION TARGET
# ==============================================================================
add_executable(videocall 
    src/main.c
    src/app/errors.h
    src/app/errors.c
)

target_include_directories(videocall PRIVATE ${PROJECT_SOURCE_DIR})

# Assemble the final binary.
# SDL3 is linked directly here to handle windowing/events. 
target_link_libraries(videocall PRIVATE 
    SDL3::SDL3 
    jpeg_decoder  
    common
    asan_config
)

# ==============================================================================
# TESTING INFRASTRUCTURE
# ==============================================================================
# Defers CTest configuration to the tests/ directory.
enable_testing()
add_subdirectory(tests)
