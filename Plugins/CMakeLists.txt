cmake_minimum_required(VERSION 3.22)

# Faderhouse voice plugins (test builds as VST3; the same DSP goes into Faderhouse later).
#
#   cmake -S Plugins -B build-plugins -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-plugins --config Release
#
# Plugins end up in build-plugins/<Name>_artefacts/Release/VST3/.

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(FaderhousePlugins VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FADERHOUSE_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
set(FADERHOUSE_PLUGIN_FORMATS "VST3" CACHE STRING "Plugin formats to build (e.g. VST3;Standalone)")

if(FADERHOUSE_JUCE_DIR)
    add_subdirectory(${FADERHOUSE_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- RNNoise (BSD-3, vendored v0.1.1 with its built-in model) ---------------------------
file(GLOB FADERHOUSE_RNNOISE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/rnnoise/src/*.c)
add_library(faderhouse_rnnoise STATIC ${FADERHOUSE_RNNOISE_SOURCES})
target_include_directories(faderhouse_rnnoise PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/rnnoise/include)
set_target_properties(faderhouse_rnnoise PROPERTIES C_STANDARD 11 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(faderhouse_rnnoise PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
    target_compile_options(faderhouse_rnnoise PRIVATE /W0)
else()
    target_compile_options(faderhouse_rnnoise PRIVATE -w)
endif()

# --- One plugin --------------------------------------------------------------------------
function(faderhouse_plugin target name code category source)
    juce_add_plugin(${target}
        PRODUCT_NAME "${name}"
        COMPANY_NAME "Faderhouse"
        COMPANY_WEBSITE "https://github.com/TibaGH/faderhouse"
        BUNDLE_ID "com.baloghproductions.faderhouse.${target}"
        PLUGIN_MANUFACTURER_CODE Fdhs
        PLUGIN_CODE ${code}
        FORMATS ${FADERHOUSE_PLUGIN_FORMATS}
        VST3_CATEGORIES ${category}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(${target} PRIVATE
        ${source}
        ${CMAKE_CURRENT_SOURCE_DIR}/Shared/FhUi.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Shared/FhDenoise.cpp)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Shared)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        FH_BUILD_AS_PLUGIN=1)
    target_link_libraries(${target}
        PRIVATE
            faderhouse_rnnoise
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)   # no LTO: builds much faster, these are light
endfunction()

faderhouse_plugin(FaderhouseEQ         "Faderhouse EQ"         FhEq "Fx;EQ"          ${CMAKE_CURRENT_SOURCE_DIR}/EQ/EQ.cpp)
faderhouse_plugin(FaderhouseCompressor "Faderhouse Compressor" FhCo "Fx;Dynamics"    ${CMAKE_CURRENT_SOURCE_DIR}/Compressor/Compressor.cpp)
faderhouse_plugin(FaderhouseLimiter    "Faderhouse Limiter"    FhLi "Fx;Dynamics"    ${CMAKE_CURRENT_SOURCE_DIR}/Limiter/Limiter.cpp)
faderhouse_plugin(FaderhouseGate       "Faderhouse Gate"       FhGa "Fx;Dynamics"    ${CMAKE_CURRENT_SOURCE_DIR}/Gate/Gate.cpp)
faderhouse_plugin(FaderhouseDenoise    "Faderhouse Denoise"    FhDn "Fx;Restoration" ${CMAKE_CURRENT_SOURCE_DIR}/Denoise/Denoise.cpp)
faderhouse_plugin(FaderhouseVoice      "Faderhouse Voice"      FhVo "Fx"             ${CMAKE_CURRENT_SOURCE_DIR}/VoiceStrip/VoiceStrip.cpp)
