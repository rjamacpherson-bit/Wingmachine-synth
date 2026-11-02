cmake_minimum_required(VERSION 3.22)
project(WingMachine VERSION 0.8.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static runtime on Windows so the .vst3 has no Visual C++ redistributable dependency
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(WingMachine
    COMPANY_NAME "Feather Synth"
    PLUGIN_MANUFACTURER_CODE Fsyn
    PLUGIN_CODE Wing
    FORMATS VST3
    PRODUCT_NAME "Wing Machine"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(WingMachine PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(WingMachine PRIVATE Source)
target_compile_definitions(WingMachine PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(WingMachine PRIVATE
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# the engine on its own (no JUCE), for a quick offline check: render every creature and season and look for NaNs
add_executable(engine_test test/engine_test.cpp)
target_include_directories(engine_test PRIVATE Source)
