cmake_minimum_required(VERSION 3.22)
project(JunkDrawer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch JUCE (override with -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE for an offline checkout)
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(JunkAssets SOURCES JunkDrawer/Assets/ffa_mark.png
        JunkDrawer/Assets/fonts/PlexSans-Regular.ttf JunkDrawer/Assets/fonts/PlexSans-SemiBold.ttf
        JunkDrawer/Assets/fonts/PlexMono-Regular.ttf JunkDrawer/Assets/fonts/PlexMono-Medium.ttf
        JunkDrawer/Assets/fonts/Fraunces-Medium.ttf JunkDrawer/Assets/fonts/SpecialElite-Regular.ttf)

juce_add_plugin(JunkDrawer
    COMPANY_NAME            "Fading Fractal Audio"
    PLUGIN_MANUFACTURER_CODE Frfa
    PLUGIN_CODE             Jdw1
    FORMATS                 VST3 Standalone
    BUNDLE_ID               com.fadingfractalaudio.junkdrawer
    PRODUCT_NAME            "Junk Drawer"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    VST3_CATEGORIES         Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(JunkDrawer PRIVATE
    JunkDrawer/Source/PluginProcessor.cpp
    JunkDrawer/Source/PluginEditor.cpp)

target_compile_definitions(JunkDrawer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(JunkDrawer
    PRIVATE juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_audio_formats
            JunkAssets
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

# Offline UI snapshots and layout check (no audio device needed):  cmake -DJD_BUILD_TESTS=ON
# On a machine without a display run it under xvfb-run.
option(JD_BUILD_TESTS "Build UI snapshot tool" OFF)
if(JD_BUILD_TESTS)
    juce_add_console_app(JunkUiSnapshot PRODUCT_NAME "JunkUiSnapshot")
    target_sources(JunkUiSnapshot PRIVATE
        JunkDrawer/Tests/UiSnapshot.cpp
        JunkDrawer/Source/PluginProcessor.cpp
        JunkDrawer/Source/PluginEditor.cpp)
    target_include_directories(JunkUiSnapshot PRIVATE JunkDrawer/Source)
    target_compile_definitions(JunkUiSnapshot PRIVATE
        JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Build_Standalone=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(JunkUiSnapshot PRIVATE
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp JunkAssets
        juce::juce_recommended_config_flags)

    juce_add_console_app(JunkEngineTest PRODUCT_NAME "JunkEngineTest")
    target_sources(JunkEngineTest PRIVATE
        JunkDrawer/Tests/EngineTest.cpp
        JunkDrawer/Source/PluginProcessor.cpp
        JunkDrawer/Source/PluginEditor.cpp)
    target_include_directories(JunkEngineTest PRIVATE JunkDrawer/Source)
    target_compile_definitions(JunkEngineTest PRIVATE
        JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Build_Standalone=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(JunkEngineTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp JunkAssets
        juce::juce_recommended_config_flags)
endif()
