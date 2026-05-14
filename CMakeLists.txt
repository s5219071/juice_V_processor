cmake_minimum_required(VERSION 3.22)

# ==============================================================================
# JuiceEQ
#
# Projucer 없이 JUCE VST3 플러그인을 빌드하기 위한 CMake 설정입니다.
# 이 프로젝트는 FFT 기반 스펙트럴 EQ/레조넌스 억제기이며, 최종 산출물 이름은
# JuiceEQ.vst3 입니다.
# ==============================================================================

project(JuiceEQ VERSION 1.0.0 LANGUAGES CXX)

# JUCE 8은 C++17 이상을 요구합니다.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# macOS 빌드가 최신 OS 전용 바이너리로만 만들어지지 않도록 최소 지원 버전을 둡니다.
if (APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

# ==============================================================================
# JUCE 자동 다운로드
#
# JUICEEQ_JUCE_VERSION 값만 바꾸면 다른 JUCE 버전으로 테스트할 수 있습니다.
# ==============================================================================

include(FetchContent)

set(JUICEEQ_JUCE_VERSION "8.0.12" CACHE STRING "JUCE version to download")

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "Do not build JUCE examples" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "Do not build JUCE extras such as Projucer" FORCE)

FetchContent_Declare(
    JUCE
    URL "https://github.com/juce-framework/JUCE/archive/refs/tags/${JUICEEQ_JUCE_VERSION}.zip"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(JUCE)

# ==============================================================================
# 플러그인 타깃 생성
#
# juce_add_plugin은 JUCE가 제공하는 CMake 함수입니다.
# VST3 wrapper, 플러그인 메타데이터, 빌드 타깃을 한 번에 만들어 줍니다.
# ==============================================================================

juce_add_plugin(JuiceEQ
    VERSION 1.0.0
    COMPANY_NAME "JuiceLab"
    COMPANY_WEBSITE "https://github.com"
    COMPANY_EMAIL "juicelab@example.com"

    FORMATS VST3

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE

    PLUGIN_MANUFACTURER_CODE JLab
    PLUGIN_CODE JEQP

    PRODUCT_NAME "JuiceEQ"
    BUNDLE_ID "com.juicelab.juiceeq"
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

# CMake 기반 JUCE 프로젝트에서 <JuceHeader.h>를 사용할 수 있게 생성합니다.
juce_generate_juce_header(JuiceEQ)

target_compile_definitions(JuiceEQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
