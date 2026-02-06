cmake_minimum_required(VERSION 3.24)
project(Squeeze VERSION 0.1.0 LANGUAGES C CXX)

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

# --- Lua 5.4 ---
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua)

# Build Lua as a static library from source
set(LUA_SRC_DIR ${lua_SOURCE_DIR}/src)
file(GLOB LUA_SOURCES ${LUA_SRC_DIR}/*.c)
# Remove lua.c (standalone interpreter) and luac.c (compiler) from library
list(REMOVE_ITEM LUA_SOURCES ${LUA_SRC_DIR}/lua.c ${LUA_SRC_DIR}/luac.c)

add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${LUA_SRC_DIR})
if(UNIX)
    target_compile_definitions(lua_static PUBLIC LUA_USE_POSIX)
    target_link_libraries(lua_static PUBLIC m)
endif()

# --- sol2 (Lua C++ bindings) ---
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.3.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(sol2)

# --- Squeeze App ---
juce_add_console_app(Squeeze
    PRODUCT_NAME "Squeeze"
)

target_sources(Squeeze PRIVATE
    src/main.cpp
)

target_link_libraries(Squeeze PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    lua_static
    sol2
)

target_compile_definitions(Squeeze PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)
