cmake_minimum_required(VERSION 3.24)
project(Squeeze VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- JUCE ---
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(juce)

# --- Squeeze Core (static library) ---
add_library(squeeze_core STATIC
    src/core/Engine.cpp
    src/core/Logger.cpp
    src/core/Port.cpp
)

target_include_directories(squeeze_core PUBLIC src)

target_link_libraries(squeeze_core PUBLIC
    juce::juce_core
    juce::juce_events
)

target_compile_definitions(squeeze_core PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# --- Squeeze FFI (shared library, C ABI) ---
add_library(squeeze_ffi SHARED
    src/ffi/squeeze_ffi.cpp
)

target_include_directories(squeeze_ffi PUBLIC src)

target_link_libraries(squeeze_ffi PRIVATE
    squeeze_core
)

# --- Catch2 ---
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# --- C++ Unit Tests ---
add_executable(squeeze_unit_tests
    tests/core/LoggerTests.cpp
    tests/core/PortTests.cpp
)

target_link_libraries(squeeze_unit_tests PRIVATE
    squeeze_core
    Catch2::Catch2WithMain
)

# --- FFI Acceptance Tests ---
add_executable(squeeze_ffi_tests
    tests/ffi/LoggerFFITests.cpp
    tests/ffi/EngineFFITests.cpp
)

target_link_libraries(squeeze_ffi_tests PRIVATE
    squeeze_ffi
    Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(squeeze_unit_tests)
catch_discover_tests(squeeze_ffi_tests)
