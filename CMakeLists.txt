cmake_minimum_required(VERSION 3.22)

project(JuiceEQ VERSION 2.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

include(FetchContent)

set(JUICEEQ_JUCE_VERSION "8.0.12" CACHE STRING "JUCE version to download")
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "Do not build JUCE examples" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "Do not build JUCE extras" FORCE)

FetchContent_Declare(
    JUCE
    URL "https://github.com/juce-framework/JUCE/archive/refs/tags/${JUICEEQ_JUCE_VERSION}.zip"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(JUCE)

# Keep the internal CMake target name stable while exposing the new product
# name to DAWs and the generated VST3 bundle.
juce_add_plugin(JuiceEQ
    VERSION 2.0.1
    COMPANY_NAME "JuiceLab"
    COMPANY_WEBSITE "https://github.com"
    COMPANY_EMAIL "juicelab@example.com"

    FORMATS VST3
    VST3_CATEGORIES Fx EQ

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE

    PLUGIN_MANUFACTURER_CODE JLab
    PLUGIN_CODE JVR1

    PRODUCT_NAME "Juice Vocal Restorer"
    BUNDLE_ID "com.juicelab.juicevocalrestorer"
)

target_sources(JuiceEQ
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
)

target_link_libraries(JuiceEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

juce_generate_juce_header(JuiceEQ)

target_compile_definitions(JuiceEQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
