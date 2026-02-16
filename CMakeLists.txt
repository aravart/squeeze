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

# --- Squeeze App (hello world) ---
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
    juce::juce_events
)

target_compile_definitions(Squeeze PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)
