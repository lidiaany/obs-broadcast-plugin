# =============================================================================
# OBS Broadcast Overlay System - CMake Build Configuration
# =============================================================================
# Plugin: Broadcast Overlay System (Lower Third, GC, Ticker, Social Media, WS)
# Language: C++17
# Build System: CMake 3.16+
# API: libobs (OBS Studio SDK)
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# ── Project Name ────────────────────────────────────────────────────────────
project(obs-broadcast-plugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build Type ──────────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# ── Source Files ────────────────────────────────────────────────────────────
set(broadcast_SOURCES
    plugin-main.cpp
    broadcast-source.cpp
    broadcast-source.h
    graphics-render.cpp
    properties.cpp
    websocket-server.cpp
    websocket-server.h
)

# ── Plugin Binary ───────────────────────────────────────────────────────────
add_library(obs-broadcast-plugin MODULE
    ${broadcast_SOURCES}
)

# ── Include Directories ────────────────────────────────────────────────────
target_include_directories(obs-broadcast-plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── Link Libraries ─────────────────────────────────────────────────────────
# libobs - OBS Studio's core SDK library
target_link_libraries(obs-broadcast-plugin PRIVATE
    libobs
)

# ── Platform-Specific Libraries ────────────────────────────────────────────
if(WIN32)
    # Windows: Winsock2 for WebSocket TCP networking
    target_link_libraries(obs-broadcast-plugin PRIVATE
        ws2_32
    )
endif()

if(APPLE)
    # macOS may need additional network frameworks if needed
    # target_link_libraries(obs-broadcast-plugin PRIVATE)
endif()

# ── Compiler Flags ──────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(obs-broadcast-plugin PRIVATE
        /W3
        /utf-8
    )
else()
    target_compile_options(obs-broadcast-plugin PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
endif()

# ── Platform Definitions ────────────────────────────────────────────────────
if(WIN32)
    target_compile_definitions(obs-broadcast-plugin PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ── Output Naming ──────────────────────────────────────────────────────────
if(APPLE)
    set_target_properties(obs-broadcast-plugin PROPERTIES
        SUFFIX ".so"
        PREFIX ""
    )
elseif(WIN32)
    set_target_properties(obs-broadcast-plugin PROPERTIES
        PREFIX ""
    )
else()
    set_target_properties(obs-broadcast-plugin PROPERTIES
        PREFIX ""
    )
endif()

# ── Install Rules ──────────────────────────────────────────────────────────
if(COMMAND install_obs_plugin_with_data)
    install_obs_plugin_with_data(obs-broadcast-plugin data)
else()
    if(APPLE)
        install(TARGETS obs-broadcast-plugin
            LIBRARY DESTINATION "Library/Application Support/obs-studio/plugins/obs-broadcast-plugin/bin"
        )
    elseif(WIN32)
        install(TARGETS obs-broadcast-plugin
            RUNTIME DESTINATION "obs-studio/plugins/obs-broadcast-plugin/bin/64bit"
        )
    else()
        install(TARGETS obs-broadcast-plugin
            LIBRARY DESTINATION "lib/obs-plugins"
        )
    endif()

    install(DIRECTORY data/
        DESTINATION "share/obs/obs-plugins/obs-broadcast-plugin/data"
    )
endif()

# ── Print Build Info ────────────────────────────────────────────────────────
message(STATUS "========================================")
message(STATUS "  OBS Broadcast Overlay Plugin v${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  WebSocket: built-in (porta configurável)")
message(STATUS "========================================")
