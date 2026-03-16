cmake_minimum_required(VERSION 3.19.0)

set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/vst3sdk")

if(NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "macOS deployment target")
endif()

option(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES "Enable VST 3 Plug-in Examples" OFF)
option(SMTG_ENABLE_VST3_HOSTING_EXAMPLES "Enable VST 3 Hosting Examples" OFF)
option(SMTG_ENABLE_VSTGUI_SUPPORT "Enable VSTGUI Support" ON)
option(SMTG_CREATE_BUNDLE_FOR_WINDOWS "Create bundle for Windows" ON)

list(APPEND CMAKE_MODULE_PATH "${vst3sdk_SOURCE_DIR}/cmake/modules")

include(SMTG_VST3_SDK)

project(GuitarTuner
    VERSION 1.0.0.0
    DESCRIPTION "VST3 Guitar Tuner with MPM pitch detection"
)

smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()

set(ROOT "${vst3sdk_SOURCE_DIR}")
set(SDK_ROOT "${ROOT}")
set(public_sdk_SOURCE_DIR ${SDK_ROOT}/public.sdk)
set(pluginterfaces_SOURCE_DIR ${SDK_ROOT}/pluginterfaces)

if(SMTG_ENABLE_VSTGUI_SUPPORT)
    smtg_enable_vstgui_support(VSTGUI_SOURCE_DIR "${ROOT}/vstgui4")
endif()

include_directories(${ROOT} ${SDK_ROOT})

# SDK libraries
smtg_create_lib_base_target()
smtg_create_pluginterfaces_target()
smtg_create_public_sdk_common_target()
smtg_create_public_sdk_target()
smtg_create_public_sdk_hosting_target()

# Add utilities (moduleinfotool) and hosting (validator)
set(SDK_IDE_HOSTING_EXAMPLES_FOLDER FOLDER "Hosting-Examples")
set(SDK_IDE_UTILITIES_FOLDER FOLDER "Utilities")
add_subdirectory(${vst3sdk_SOURCE_DIR}/public.sdk/samples/vst-utilities
    ${CMAKE_BINARY_DIR}/vst-utilities)
add_subdirectory(${vst3sdk_SOURCE_DIR}/public.sdk/samples/vst-hosting
    ${CMAKE_BINARY_DIR}/vst-hosting)

# Plugin sources
set(tuner_sources
    src/plugids.h
    src/version.h
    src/processor.h
    src/processor.cpp
    src/controller.h
    src/controller.cpp
    src/entry.cpp
    src/dsp/ring_buffer.h
    src/dsp/pitch_detector.h
    src/dsp/mpm_detector.h
    src/dsp/mpm_detector.cpp
    src/ui/tuner_view.h
    src/ui/tuner_view.cpp
)

set(target GuitarTuner)

smtg_add_vst3plugin(${target} ${tuner_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target}
    PUBLIC
        cxx_std_17
)

target_include_directories(${target}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${target}
    PRIVATE
        sdk
        vstgui_support
)

if(SMTG_WIN)
    target_sources(${target}
        PRIVATE
            resources/win32resource.rc
    )
endif()

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resources/tuner_editor.uidesc
)

# Unit tests
enable_testing()
add_executable(GuitarTunerTests
    tests/test_pitch.cpp
    src/dsp/mpm_detector.cpp
)
target_include_directories(GuitarTunerTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(GuitarTunerTests PUBLIC cxx_std_17)
add_test(NAME PitchDetectorTests COMMAND GuitarTunerTests)
