# Downloads third-party code the first time you configure.
# To use a copy you already have, pass e.g. -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# JUCE: audio devices (CoreAudio / ASIO / WASAPI), plugin hosting (AU / VST3), DSP, web UI.
# Used under the AGPLv3 (free and open source). See README for what that means.
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# shine (LGPL 2): small fixed-point MP3 encoder, for "Exportar a MP3"
FetchContent_Declare(Shine
    GIT_REPOSITORY https://github.com/toots/shine.git
    GIT_TAG        3.1.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  galpon-do-not-build)
FetchContent_MakeAvailable(Shine)
file(GLOB SHINE_SOURCES "${shine_SOURCE_DIR}/src/lib/*.c")
add_library(galpon_shine STATIC ${SHINE_SOURCES})
target_include_directories(galpon_shine SYSTEM PUBLIC "${shine_SOURCE_DIR}/src/lib")
set_target_properties(galpon_shine PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
if (NOT MSVC)
    target_compile_options(galpon_shine PRIVATE -O2 -w)
endif()

# Signalsmith Stretch (MIT): time stretching and pitch shifting for "Flex" (quantize audio, follow
# tempo, tune vocals). Header-only, plus its small "linear" helper library.
FetchContent_Declare(SignalsmithStretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  galpon-do-not-build)
FetchContent_Declare(SignalsmithLinear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG        0.6.6
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  galpon-do-not-build)
FetchContent_MakeAvailable(SignalsmithStretch SignalsmithLinear)
add_library(galpon_stretch INTERFACE)
target_include_directories(galpon_stretch SYSTEM INTERFACE "${signalsmithstretch_SOURCE_DIR}" "${signalsmithlinear_SOURCE_DIR}/include" "${signalsmithlinear_SOURCE_DIR}")

# Eigen (MPL2): linear algebra for the neural networks (NAM amps, Demucs stems).
# The exact revision NAM is tested with.
FetchContent_Declare(Eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        bc3b39870ecb690a623a3f49149a358b95c5781d
    SOURCE_SUBDIR  galpon-do-not-build)
FetchContent_MakeAvailable(Eigen)

if (GALPON_WITH_STEMS)
    # demucs.cpp (MIT): Demucs v4 (Meta's music source separation network) in plain C++,
    # for "Separar en pistas" (voice, drums, bass, other). The weights (MIT, ~84 MB) are
    # downloaded by the app the first time it's used.
    FetchContent_Declare(DemucsCpp
        GIT_REPOSITORY https://github.com/sevagh/demucs.cpp.git
        GIT_TAG        f1206e9adeea103aef4a636b9e62297cf1f8e34e
        GIT_SUBMODULES ""
        SOURCE_SUBDIR  galpon-do-not-build)
    FetchContent_MakeAvailable(DemucsCpp)
    file(GLOB DEMUCS_SOURCES "${demucscpp_SOURCE_DIR}/src/*.cpp")
    add_library(galpon_demucs STATIC ${DEMUCS_SOURCES})
    target_include_directories(galpon_demucs SYSTEM PUBLIC "${demucscpp_SOURCE_DIR}/src" "${eigen_SOURCE_DIR}")
    set_target_properties(galpon_demucs PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
    target_compile_definitions(galpon_demucs PRIVATE NDEBUG)
    if (NOT MSVC)
        target_compile_options(galpon_demucs PRIVATE -O3 -w)
    endif()
endif()

if (GALPON_WITH_NAM)
    # Neural Amp Modeler core (MIT): runs .nam amp captures, like the ones on Tone3000.
    FetchContent_Declare(NAMCore
        GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
        GIT_TAG        v0.6.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  galpon-do-not-build)
    FetchContent_MakeAvailable(NAMCore)


    file(GLOB NAM_SOURCES_TOP "${namcore_SOURCE_DIR}/NAM/*.cpp")
    file(GLOB NAM_SOURCES_SUB "${namcore_SOURCE_DIR}/NAM/*/*.cpp")
    # OBJECT library: NAM registers its model types with static initialisers, which a
    # static library would let the linker drop.
    add_library(galpon_nam OBJECT ${NAM_SOURCES_TOP} ${NAM_SOURCES_SUB})
    target_include_directories(galpon_nam SYSTEM PUBLIC
        "${namcore_SOURCE_DIR}"
        "${namcore_SOURCE_DIR}/NAM"
        "${namcore_SOURCE_DIR}/Dependencies/nlohmann"
        "${eigen_SOURCE_DIR}")
    target_compile_definitions(galpon_nam PUBLIC NAM_SAMPLE_FLOAT)
    set_target_properties(galpon_nam PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if (NOT MSVC)
        target_compile_options(galpon_nam PRIVATE -O3 -w)
    endif()
endif()

# ---------------------------------------------------------------------------
# Windows only
# ---------------------------------------------------------------------------
set(GALPON_ASIO_ENABLED OFF)
if (WIN32)
    # WebView2 (the interface is a web page inside the app): the static loader from Microsoft's
    # NuGet package, so nothing extra has to be installed. Downloaded once.
    if (NOT JUCE_WEBVIEW2_PACKAGE_LOCATION)
        set(_wv2_version 1.0.3485.44)
        set(_wv2_root "${CMAKE_BINARY_DIR}/_deps/webview2")
        set(_wv2_dir "${_wv2_root}/Microsoft.Web.WebView2.${_wv2_version}")
        if (NOT EXISTS "${_wv2_dir}/build/native/include/WebView2.h")
            file(DOWNLOAD "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${_wv2_version}"
                 "${_wv2_root}/webview2.nupkg" SHOW_PROGRESS STATUS _wv2_status TLS_VERIFY ON)
            list(GET _wv2_status 0 _wv2_code)
            if (NOT _wv2_code EQUAL 0)
                message(FATAL_ERROR "Could not download WebView2 from nuget.org: ${_wv2_status}")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${_wv2_root}/webview2.nupkg" DESTINATION "${_wv2_dir}")
        endif()
        set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${_wv2_root}" CACHE PATH "Folder holding the Microsoft.Web.WebView2 package" FORCE)
    endif()

    # ASIO: the low-latency driver audio interfaces ship for Windows. Steinberg's SDK is
    # dual-licensed (proprietary or GPLv3); Galpon uses it under the GPLv3. Not redistributed
    # here: downloaded from Steinberg when you build.
    if (GALPON_WITH_ASIO)
        set(_asio_root "${CMAKE_BINARY_DIR}/_deps/asiosdk")
        if (NOT EXISTS "${_asio_root}/ASIOSDK/common/iasiodrv.h")
            file(DOWNLOAD "https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip"
                 "${_asio_root}/asiosdk.zip" SHOW_PROGRESS STATUS _asio_status TLS_VERIFY ON
                 EXPECTED_HASH SHA256=d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca)
            list(GET _asio_status 0 _asio_code)
            if (_asio_code EQUAL 0)
                file(ARCHIVE_EXTRACT INPUT "${_asio_root}/asiosdk.zip" DESTINATION "${_asio_root}")
            else()
                message(WARNING "Could not download the ASIO SDK (${_asio_status}): building with WASAPI only.")
            endif()
        endif()
        if (EXISTS "${_asio_root}/ASIOSDK/common/iasiodrv.h")
            set(GALPON_ASIO_ENABLED ON)
            set(GALPON_ASIO_INCLUDE "${_asio_root}/ASIOSDK/common")
        endif()
    endif()
endif()
