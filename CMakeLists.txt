cmake_minimum_required(VERSION 3.22)

# Universal binary on Mac is optional; by default we build for the machine you are on.
project(Galpon VERSION 0.11.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

if (MSVC)
    # One .exe that runs on any Windows 10/11 PC without installing the Visual C++ runtime
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # sources are UTF-8 (Spanish text); big files; build in parallel
    add_compile_options(/utf-8 /bigobj /MP)
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

option(GALPON_WITH_NAM   "Built-in Neural Amp Modeler (.nam captures, e.g. from Tone3000)" ON)
option(GALPON_WITH_STEMS "Stem separation with Demucs (demucs.cpp)" ON)
option(GALPON_BUILD_APP  "Build the desktop app"                                         ON)
option(GALPON_BUILD_TESTS "Build the engine tests (offline, no audio device needed)"     ON)
option(GALPON_WITH_ASIO  "ASIO audio on Windows (downloads Steinberg's ASIO SDK, used under GPLv3)" ON)

include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# Engine sources: shared by the app and the tests
# ---------------------------------------------------------------------------
set(GALPON_ENGINE_SOURCES
    src/engine/Model.cpp
    src/engine/BuiltinInstruments.cpp
    src/engine/BuiltinEffects.cpp
    src/engine/NamAmp.cpp
    src/engine/Drummer.cpp
    src/engine/PluginHost.cpp
    src/engine/Engine.cpp
    src/engine/EngineApi.cpp
    src/engine/Loops.cpp
    src/engine/Encoders.cpp
    src/engine/Analysis.cpp
    src/engine/Sampler.cpp
    src/engine/SoundLibrary.cpp
    src/engine/Flex.cpp
    src/engine/Stems.cpp
    src/engine/Recorder.cpp
    src/engine/Yaml.cpp
    src/engine/Feedpak.cpp
    src/engine/GamePlayer.cpp
    src/engine/NoteScorer.cpp
    src/engine/Tone3000.cpp
)

set(GALPON_JUCE_DEFS
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=0
    JUCE_ASIO=$<BOOL:${GALPON_ASIO_ENABLED}>
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

function(galpon_link_common target)
    target_include_directories(${target} PRIVATE src)
    if (GALPON_ASIO_ENABLED)
        target_include_directories(${target} SYSTEM PRIVATE "${GALPON_ASIO_INCLUDE}")
    endif()
    target_link_libraries(${target} PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
        galpon_shine
        galpon_stretch)
    if (GALPON_WITH_STEMS)
        target_link_libraries(${target} PRIVATE galpon_demucs)
        target_compile_definitions(${target} PRIVATE GALPON_WITH_STEMS=1)
    else()
        target_compile_definitions(${target} PRIVATE GALPON_WITH_STEMS=0)
    endif()
    if (GALPON_WITH_NAM)
        target_link_libraries(${target} PRIVATE galpon_nam)
        target_compile_definitions(${target} PRIVATE GALPON_WITH_NAM=1)
    else()
        target_compile_definitions(${target} PRIVATE GALPON_WITH_NAM=0)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
if (GALPON_BUILD_APP)
    juce_add_gui_app(Galpon
        PRODUCT_NAME "Galpon"
        COMPANY_NAME "Galpon"
        BUNDLE_ID "app.galpon.studio"
        VERSION ${PROJECT_VERSION}
        NEEDS_WEB_BROWSER TRUE
        NEEDS_WEBVIEW2 TRUE
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Galpon usa la entrada de audio para grabar voces e instrumentos."
        HARDENED_RUNTIME_ENABLED FALSE
        DOCUMENT_EXTENSIONS galpon)

    juce_add_binary_data(GalponUI
        HEADER_NAME GalponUI.h
        NAMESPACE GalponUI
        SOURCES
            ui/index.html
            ui/styles.css
            ui/bridge.js
            ui/app.js
            ui/timeline.js
            ui/panels.js
            ui/game.js)

    target_sources(Galpon PRIVATE
        ${GALPON_ENGINE_SOURCES}
        src/app/Main.cpp
        src/app/MainComponent.cpp
        src/app/PluginWindows.cpp)

    target_compile_definitions(Galpon PRIVATE
        ${GALPON_JUCE_DEFS}
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Galpon,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Galpon,JUCE_VERSION>")

    galpon_link_common(Galpon)
    target_link_libraries(Galpon PRIVATE GalponUI juce::juce_gui_extra)
endif()

# ---------------------------------------------------------------------------
# Tests: render songs offline and check the results
# ---------------------------------------------------------------------------
if (GALPON_BUILD_TESTS)
    juce_add_console_app(GalponTests PRODUCT_NAME "GalponTests")
    target_sources(GalponTests PRIVATE ${GALPON_ENGINE_SOURCES} tests/EngineTests.cpp)
    target_compile_definitions(GalponTests PRIVATE ${GALPON_JUCE_DEFS} JUCE_WEB_BROWSER=0
        GALPON_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    galpon_link_common(GalponTests)
    if (GALPON_WITH_NAM)
        target_compile_definitions(GalponTests PRIVATE GALPON_NAM_EXAMPLE="${namcore_SOURCE_DIR}/example_models/wavenet.nam")
    endif()
endif()
