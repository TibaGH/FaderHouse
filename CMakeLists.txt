cmake_minimum_required(VERSION 3.22)

# Core Audio process taps need macOS 14.2 or later.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2" CACHE STRING "Minimum macOS version")

project(Faderhouse VERSION 1.0.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE: either point FADERHOUSE_JUCE_DIR at a local checkout, or it is downloaded.
# ---------------------------------------------------------------------------
set(FADERHOUSE_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")

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

juce_add_gui_app(Faderhouse
    PRODUCT_NAME "Faderhouse"
    COMPANY_NAME "Balogh Productions"
    BUNDLE_ID "com.baloghproductions.faderhouse"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Faderhouse uses your audio interface input for the microphone channel."
    # NSAudioCaptureUsageDescription is required for Core Audio process taps (per-app capture).
    PLIST_TO_MERGE "<plist><dict><key>NSAudioCaptureUsageDescription</key><string>Faderhouse captures the audio of the apps you assign to its channels so you can mix them.</string><key>LSApplicationCategoryType</key><string>public.app-category.music</string></dict></plist>"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(Faderhouse PRIVATE
    Source/Main.cpp
    Source/Engine/PluginChain.cpp
    Source/Engine/ExtraOutputDevice.cpp
    Source/Engine/MixerEngine.cpp
    Source/Engine/Session.cpp
    Source/Engine/PluginScanner.cpp
    Source/Update/Updater.cpp
    Source/UI/FaderhouseLookAndFeel.cpp
    Source/UI/PluginChainEditor.cpp
    Source/UI/ChannelStrip.cpp
    Source/UI/OutputStrip.cpp
    Source/UI/SettingsComponent.cpp
    Source/UI/MainComponent.cpp
    Source/UI/VirtualMicPanel.cpp
    Source/UI/ConsoleFader.cpp
    Source/UI/ChannelIcons.cpp)

if(APPLE)
    target_sources(Faderhouse PRIVATE
        Source/Capture/AppCaptureMac.mm
        Source/VirtualMic/VirtualMicInstallerMac.mm
        Source/UI/AppIconsMac.mm)
    target_link_libraries(Faderhouse PRIVATE "-framework CoreAudio" "-framework AppKit" "-framework Foundation")
elseif(WIN32)
    target_sources(Faderhouse PRIVATE
        Source/Capture/AppCaptureStub.cpp            # Windows backend: phase 3
        Source/VirtualMic/VirtualMicInstallerStub.cpp
        Source/UI/AppIconsStub.cpp)
else()
    target_sources(Faderhouse PRIVATE
        Source/Capture/AppCaptureStub.cpp
        Source/VirtualMic/VirtualMicInstallerStub.cpp
        Source/UI/AppIconsStub.cpp)
endif()

# ---------------------------------------------------------------------------
# "Mic FX": our own virtual microphone (Core Audio HAL plug-in, macOS).
# Built as FaderhouseMicFX.driver and shipped inside Faderhouse.app/Contents/Resources;
# the app installs it into /Library/Audio/Plug-Ins/HAL when you click Install.
# Uses libASPL (MIT) for the AudioServerPlugIn plumbing - downloaded automatically.
# ---------------------------------------------------------------------------
if(APPLE)
    include(FetchContent)
    FetchContent_Populate(libaspl
        QUIET
        GIT_REPOSITORY https://github.com/gavv/libASPL.git
        GIT_TAG        v3.1.2
        GIT_SHALLOW    TRUE
        SOURCE_DIR     ${CMAKE_BINARY_DIR}/_deps/libaspl-src
        BINARY_DIR     ${CMAKE_BINARY_DIR}/_deps/libaspl-build
        SUBBUILD_DIR   ${CMAKE_BINARY_DIR}/_deps/libaspl-subbuild)

    # libASPL's generated sources are checked in, so we compile them directly
    # (its own CMakeLists needs git tags + python, which we want to avoid).
    file(GLOB FADERHOUSE_ASPL_SOURCES CONFIGURE_DEPENDS ${libaspl_SOURCE_DIR}/src/*.cpp)
    add_library(faderhouse_aspl STATIC ${FADERHOUSE_ASPL_SOURCES})
    target_include_directories(faderhouse_aspl
        PUBLIC  ${libaspl_SOURCE_DIR}/include
        PRIVATE ${libaspl_SOURCE_DIR}/src)
    set_target_properties(faderhouse_aspl PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
    target_compile_options(faderhouse_aspl PRIVATE -w)
    target_link_libraries(faderhouse_aspl PUBLIC "-framework CoreFoundation" "-framework CoreAudio")

    add_library(FaderhouseMicFX MODULE Source/VirtualMic/MicFXDriver.cpp)
    target_include_directories(FaderhouseMicFX PRIVATE Source/VirtualMic)
    target_link_libraries(FaderhouseMicFX PRIVATE faderhouse_aspl)
    set_target_properties(FaderhouseMicFX PROPERTIES
        CXX_STANDARD 17
        BUNDLE TRUE
        BUNDLE_EXTENSION "driver"
        PREFIX ""
        SUFFIX ""
        OUTPUT_NAME "FaderhouseMicFX"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Source/VirtualMic/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "FaderhouseMicFX"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.baloghproductions.faderhouse.micfx"
        MACOSX_BUNDLE_COPYRIGHT "Faderhouse")

    # Ship the driver inside the app.
    add_dependencies(Faderhouse FaderhouseMicFX)
    add_custom_command(TARGET Faderhouse POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -r -f "$<TARGET_BUNDLE_CONTENT_DIR:Faderhouse>/Resources/FaderhouseMicFX.driver"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:FaderhouseMicFX>" "$<TARGET_BUNDLE_CONTENT_DIR:Faderhouse>/Resources/FaderhouseMicFX.driver"
        COMMENT "Bundling FaderhouseMicFX.driver into Faderhouse.app"
        VERBATIM)
endif()

target_include_directories(Faderhouse PRIVATE Source)

target_compile_definitions(Faderhouse PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_ASIO=0   # set to 1 on Windows once you have the Steinberg ASIO SDK
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Faderhouse,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Faderhouse,JUCE_VERSION>")

target_link_libraries(Faderhouse
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_gui_extra
        juce::juce_dsp
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
