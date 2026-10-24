# Renders Resources/AppIcon.png and the DMG backgrounds from Source/UI/AppLogo.h.
#   cmake -S Tools/IconRender -B build-icons && cmake --build build-icons
#   build-icons/IconRender_artefacts/IconRender Resources
cmake_minimum_required(VERSION 3.22)
project(IconRender VERSION 0.1)
set(CMAKE_CXX_STANDARD 20)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.4 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
set(SRC ${CMAKE_CURRENT_LIST_DIR}/../../Source)
juce_add_console_app(IconRender)
target_sources(IconRender PRIVATE main.cpp preview.cpp ${SRC}/UI/ChannelIcons.cpp ${SRC}/UI/AppIconsStub.cpp)
target_include_directories(IconRender PRIVATE ${SRC})
target_compile_definitions(IconRender PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(IconRender PRIVATE juce::juce_gui_basics juce::juce_audio_processors juce::juce_audio_utils)
