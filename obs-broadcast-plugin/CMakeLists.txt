cmake_minimum_required(VERSION 3.16)

project(obs-broadcast-plugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Find OBS SDK ──────────────────────────────────────────────────────────
find_package(libobs REQUIRED)

# ── Sources ───────────────────────────────────────────────────────────────
set(SOURCES
    src/plugin-main.cpp
    src/broadcast-source.cpp
    src/graphics-render.cpp
    src/properties.cpp
    src/websocket-server.cpp
)

set(HEADERS
    src/broadcast-source.h
    src/websocket-server.h
)

# ── Plugin library ────────────────────────────────────────────────────────
add_library(${PROJECT_NAME} MODULE
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OBS::libobs
)

# ── Platform-specific ────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ── Compiler flags ────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3
        /utf-8
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
endif()

# ── Output naming ────────────────────────────────────────────────────────
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
)

# ── Install rules ────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION obs-plugins
    RUNTIME DESTINATION obs-plugins
)

install(DIRECTORY data/
    DESTINATION data/obs-plugins/${PROJECT_NAME}
)

message(STATUS "========================================")
message(STATUS "  OBS Broadcast Overlay Plugin v${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  WebSocket: built-in (port 8080)")
message(STATUS "========================================")
