cmake_minimum_required(VERSION 3.25)

project(Arclight
    VERSION 0.2.0
    DESCRIPTION "Arclight - a cross-platform learning engine"
    LANGUAGES C CXX)

# The Metal backend is written in Objective-C++ (.mm).
if(APPLE)
    enable_language(OBJCXX)
endif()

option(ARCLIGHT_BUILD_TESTS "Build the Arclight unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# compile_commands.json for clangd and other tooling; ignored by VS and Xcode.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ArclightWarnings)
include(ArclightDependencies)
include(ArclightShaders)

add_subdirectory(Engine)
add_subdirectory(Editor)

if(ARCLIGHT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

include(ArclightPackaging)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Arclight)
