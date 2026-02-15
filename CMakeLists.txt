cmake_minimum_required(VERSION 3.14)

# Allow older cmake_minimum_required in dependencies (libADLMIDI, rtmidi)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

project(RetroWaveMIDIProxy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Build options
option(BUILD_GUI "Build the Qt5 GUI application" ON)
option(BUILD_CLI "Build the headless CLI daemon" ON)

# CPM dependency manager
include(cmake/CPM.cmake)
include(cmake/PatchFile.cmake)

CPMAddPackage(
    NAME libADLMIDI
    GITHUB_REPOSITORY Wohlstand/libADLMIDI
    GIT_TAG 84d27bc2bdbd6dd249537a7f7d2450cbd402482e
    GIT_SHALLOW ON
)

CPMAddPackage(
    NAME rtmidi
    GITHUB_REPOSITORY thestk/rtmidi
    GIT_TAG 6.0.0
    GIT_SHALLOW ON
)

patch_file(
    ${libADLMIDI_SOURCE_DIR}/src/midi_sequencer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/patch/00-adlmidi-nuke-midisequencer-private.patch
)

target_compile_definitions(ADLMIDI_static PUBLIC -Dprivate=public)

# Standalone WOPL bank file parser (no synth engine, just the file format)
add_library(wopl_parser STATIC
    ${libADLMIDI_SOURCE_DIR}/src/wopl/wopl_file.c
)
target_include_directories(wopl_parser PUBLIC
    ${libADLMIDI_SOURCE_DIR}/src/wopl
)

# Core library (no Qt dependency)
add_subdirectory(core)

# Banks library (embedded WOPL files)
add_subdirectory(banks)

# GUI (Qt5)
if(BUILD_GUI)
    add_subdirectory(gui)
endif()

# CLI daemon (no Qt dependency)
if(BUILD_CLI)
    add_subdirectory(cli)
endif()

# OPL3 parameter editor panel (Qt5, no libADLMIDI dependency)
option(BUILD_PANEL "Build the OPL3 parameter editor panel" ON)
if(BUILD_PANEL)
    add_subdirectory(panel)
endif()
