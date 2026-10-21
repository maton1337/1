cmake_minimum_required(VERSION 3.22)

project(BridgeRack VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

option(BRIDGERACK_BUILD_APP   "Build the BridgeRack application" ON)
option(BRIDGERACK_BUILD_TESTS "Build the headless engine tests"  ON)

# Path to the legacy VST2 SDK (folder containing pluginterfaces/vst2.x).
# Steinberg no longer distributes it; leave empty to build without VST2 hosting.
set(BRIDGERACK_VST2_SDK_PATH "" CACHE PATH "Path to the VST2 SDK (optional)")

#==============================================================================
# JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.3
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

#==============================================================================
# All JUCE modules and the engine are compiled once into this static library,
# which both the app and the headless tests link against.
add_library(bridgerack_core STATIC
    src/engine/ClockBridge.cpp
    src/engine/Rack.cpp
    src/engine/RackSet.cpp
    src/engine/AudioEngine.cpp
    src/engine/PluginHost.cpp
    src/engine/Session.cpp)

set(_vst2 0)
if(BRIDGERACK_VST2_SDK_PATH)
    set(_vst2 1)
    target_include_directories(bridgerack_core PRIVATE "${BRIDGERACK_VST2_SDK_PATH}")
endif()

if(APPLE)
    set(_linux 0)
    set(_apple 1)
elseif(UNIX)
    set(_linux 1)
    set(_apple 0)
else()
    set(_linux 0)
    set(_apple 0)
endif()

target_include_directories(bridgerack_core PUBLIC src)

target_compile_definitions(bridgerack_core PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=${_vst2}
    JUCE_PLUGINHOST_AU=${_apple}
    JUCE_PLUGINHOST_LV2=${_linux}
    JUCE_JACK=${_linux}
    JUCE_ALSA=${_linux}
    JUCE_USE_XINPUT=0)

target_link_libraries(bridgerack_core
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

set_target_properties(bridgerack_core PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# Expose the JUCE include paths and module config to consumers.
target_compile_definitions(bridgerack_core INTERFACE
    $<TARGET_PROPERTY:bridgerack_core,COMPILE_DEFINITIONS>)
target_include_directories(bridgerack_core INTERFACE
    $<TARGET_PROPERTY:bridgerack_core,INCLUDE_DIRECTORIES>)

#==============================================================================
if(BRIDGERACK_BUILD_APP)
    juce_add_gui_app(BridgeRack
        PRODUCT_NAME                  "BridgeRack"
        COMPANY_NAME                  "BridgeRack"
        BUNDLE_ID                     "org.bridgerack.app"
        VERSION                       ${PROJECT_VERSION}
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT    "BridgeRack needs audio input access to process incoming signals.")

    target_sources(BridgeRack PRIVATE
        src/Main.cpp
        src/ui/DeviceSetupPanel.cpp
        src/ui/LookAndFeel.cpp
        src/ui/MainComponent.cpp
        src/ui/PluginWindows.cpp
        src/ui/ChannelStrip.cpp
        src/ui/RoutingMatrix.cpp)

    target_compile_definitions(BridgeRack PRIVATE
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BridgeRack,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BridgeRack,JUCE_VERSION>")

    target_link_libraries(BridgeRack PRIVATE bridgerack_core juce::juce_recommended_lto_flags)
endif()

#==============================================================================
if(BRIDGERACK_BUILD_TESTS)
    add_executable(BridgeRackTests tests/TestMain.cpp)
    target_link_libraries(BridgeRackTests PRIVATE bridgerack_core)
    enable_testing()
    add_test(NAME engine COMMAND BridgeRackTests)
endif()
