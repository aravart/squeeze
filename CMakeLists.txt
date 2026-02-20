cmake_minimum_required(VERSION 3.24)
project(Squeeze VERSION 0.3.0 LANGUAGES C CXX)

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
    src/core/AudioDevice.cpp
    src/core/Buffer.cpp
    src/core/BufferLibrary.cpp
    src/core/Bus.cpp
    src/core/Chain.cpp
    src/core/Engine.cpp
    src/core/Logger.cpp
    src/core/MidiDeviceManager.cpp
    src/core/MidiRouter.cpp
    src/core/PlaybackCursor.cpp
    src/core/PlayerProcessor.cpp
    src/core/PluginManager.cpp
    src/core/PluginProcessor.cpp
    src/core/Source.cpp
    src/core/Transport.cpp
    src/core/EventScheduler.cpp
    src/core/ClockDispatch.cpp
    src/core/PerfMonitor.cpp
)

target_include_directories(squeeze_core PUBLIC src)

target_link_libraries(squeeze_core PUBLIC
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
)

target_compile_definitions(squeeze_core PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
)

# --- Squeeze FFI (shared library, C ABI) ---
add_library(squeeze_ffi SHARED
    src/ffi/squeeze_ffi.cpp
    src/gui/EditorManager.cpp
)

target_include_directories(squeeze_ffi PUBLIC src)

target_link_libraries(squeeze_ffi PRIVATE
    squeeze_core
    juce::juce_gui_basics
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
    tests/core/SPSCQueueTests.cpp
    tests/core/CommandQueueTests.cpp
    tests/core/MidiRouterTests.cpp
    tests/core/EngineTests.cpp
    tests/core/PluginManagerTests.cpp
    tests/core/ProcessorTests.cpp
    tests/core/ChainTests.cpp
    tests/core/SourceTests.cpp
    tests/core/BusTests.cpp
    tests/core/TransportTests.cpp
    tests/core/EventSchedulerTests.cpp
    tests/core/ClockDispatchTests.cpp
    tests/core/PerfMonitorTests.cpp
    tests/core/BufferTests.cpp
    tests/core/BufferLibraryTests.cpp
    tests/core/PlaybackCursorTests.cpp
    tests/core/PlayerProcessorTests.cpp
)

target_link_libraries(squeeze_unit_tests PRIVATE
    squeeze_core
    Catch2::Catch2WithMain
)

# --- FFI Acceptance Tests ---
add_executable(squeeze_ffi_tests
    tests/ffi/LoggerFFITests.cpp
    tests/ffi/EngineFFITests.cpp
    tests/ffi/PluginManagerFFITests.cpp
    tests/ffi/AudioDeviceFFITests.cpp
    tests/ffi/MidiDeviceManagerFFITests.cpp
    tests/ffi/EditorFFITests.cpp
    tests/ffi/TransportFFITests.cpp
    tests/ffi/EventSchedulerFFITests.cpp
    tests/ffi/ClockDispatchFFITests.cpp
    tests/ffi/PerfMonitorFFITests.cpp
    tests/ffi/BufferFFITests.cpp
)

target_link_libraries(squeeze_ffi_tests PRIVATE
    squeeze_ffi
    squeeze_core
    Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(squeeze_unit_tests)
catch_discover_tests(squeeze_ffi_tests)
