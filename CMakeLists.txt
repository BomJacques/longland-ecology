cmake_minimum_required(VERSION 3.25)
project(LonglandEcologyCore VERSION 0.1.0 LANGUAGES C CXX)

set(LONGLAND_JUCE_ROOT "C:/SDK/JUCE"
    CACHE PATH "Path to the JUCE 9 source tree")

option(LONGLAND_ENABLE_REAL_DSP "Build Phase 3-6 real species DSP" ON)
set(LONGLAND_PHASE2_GATE_ACCEPTED TRUE CACHE BOOL "User-reviewed Phase 2 gate acceptance")
if(LONGLAND_ENABLE_REAL_DSP AND NOT LONGLAND_PHASE2_GATE_ACCEPTED)
    message(FATAL_ERROR "Real DSP requires accepted Phase 2 gain-only gate evidence.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(longland_ecology_core
    Source/analysis/ResourceAnalyzer.cpp
    Source/ecology/EcologyEngine.cpp
    Source/ecology/Phase4EcologyModel.cpp
    Source/ecology/Phase5EcologyModel.cpp
    Source/ecology/Phase6EcologyModel.cpp
    Source/ecology/PredatorBloomScenario.cpp
    Source/ecology/PredatorPreyModel.cpp
    Source/ecology/PredatorPreyScenario.cpp
    Source/dsp/BloomProcessor.cpp
    Source/dsp/DummyGainProcessor.cpp
    Source/dsp/GrazerProcessor.cpp
    Source/dsp/DecomposerProcessor.cpp
    Source/dsp/PredatorProcessor.cpp
    Source/dsp/ScavengerProcessor.cpp
    Source/dsp/ParasiteProcessor.cpp)

target_include_directories(longland_ecology_core PUBLIC Source)

if(MSVC)
    target_compile_options(longland_ecology_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(longland_ecology_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(longland_phase2_gauntlet Tests/GauntletMain.cpp)
target_link_libraries(longland_phase2_gauntlet PRIVATE longland_ecology_core)

add_executable(ecology_sim_cli Tools/EcologySimCli.cpp)
target_link_libraries(ecology_sim_cli PRIVATE longland_ecology_core)

add_executable(phase3_render_cli Tools/Phase3RenderCli.cpp)
target_link_libraries(phase3_render_cli PRIVATE longland_ecology_core)

add_executable(phase4_render_cli Tools/Phase4RenderCli.cpp)
target_link_libraries(phase4_render_cli PRIVATE longland_ecology_core)

add_executable(predator_processor_tests Tests/PredatorProcessorTests.cpp)
target_link_libraries(predator_processor_tests PRIVATE longland_ecology_core)

add_executable(bloom_processor_tests Tests/BloomProcessorTests.cpp)
target_link_libraries(bloom_processor_tests PRIVATE longland_ecology_core)

add_executable(grazer_processor_tests Tests/GrazerProcessorTests.cpp)
target_link_libraries(grazer_processor_tests PRIVATE longland_ecology_core)

add_executable(scavenger_processor_tests Tests/ScavengerProcessorTests.cpp)
target_link_libraries(scavenger_processor_tests PRIVATE longland_ecology_core)
if(MSVC)
    target_compile_options(scavenger_processor_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(scavenger_processor_tests PRIVATE -UNDEBUG)
endif()

add_executable(parasite_processor_tests Tests/ParasiteProcessorTests.cpp)
target_link_libraries(parasite_processor_tests PRIVATE longland_ecology_core)

add_executable(phase5_ecology_tests Tests/Phase5EcologyTests.cpp)
target_link_libraries(phase5_ecology_tests PRIVATE longland_ecology_core)

add_executable(phase5_integration_tests Tests/Phase5IntegrationTests.cpp)
target_link_libraries(phase5_integration_tests PRIVATE longland_ecology_core)

add_executable(decomposer_processor_tests Tests/DecomposerProcessorTests.cpp)
target_link_libraries(decomposer_processor_tests PRIVATE longland_ecology_core)

add_executable(phase6_ecology_tests Tests/Phase6EcologyTests.cpp)
target_link_libraries(phase6_ecology_tests PRIVATE longland_ecology_core)

add_executable(phase6_integration_tests Tests/Phase6IntegrationTests.cpp)
target_link_libraries(phase6_integration_tests PRIVATE longland_ecology_core)

if(EXISTS "${LONGLAND_JUCE_ROOT}/CMakeLists.txt")
    add_subdirectory("${LONGLAND_JUCE_ROOT}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

    juce_add_plugin(LonglandEcology
        COMPANY_NAME "Longland"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE Lngd
        PLUGIN_CODE Leco
        FORMATS VST3
        PRODUCT_NAME "Longland Ecology")

    target_sources(LonglandEcology PRIVATE
        Source/plugin/PluginProcessor.cpp
        Source/plugin/PluginProcessor.h
        Source/plugin/PluginEditor.cpp
        Source/plugin/PluginEditor.h)

    target_compile_definitions(LonglandEcology PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(LonglandEcology PRIVATE
        longland_ecology_core
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)
else()
    message(WARNING "JUCE not found at LONGLAND_JUCE_ROOT; core gauntlet remains buildable")
endif()

enable_testing()
add_test(NAME phase2_gain_only_gate COMMAND longland_phase2_gauntlet)
set_tests_properties(phase2_gain_only_gate PROPERTIES LABELS "phase1;phase2_gate")
add_test(NAME phase3_predator_audio COMMAND predator_processor_tests)
add_test(NAME phase3_bloom_audio COMMAND bloom_processor_tests)
set_tests_properties(phase3_predator_audio phase3_bloom_audio PROPERTIES LABELS "phase3_audio")
add_test(NAME phase4_grazer_audio COMMAND grazer_processor_tests)
add_test(NAME phase4_scavenger_audio COMMAND scavenger_processor_tests)
set_tests_properties(phase4_grazer_audio phase4_scavenger_audio PROPERTIES LABELS "phase4_audio")
add_test(NAME phase5_parasite_audio COMMAND parasite_processor_tests)
add_test(NAME phase5_host_ecology COMMAND phase5_ecology_tests)
add_test(NAME phase5_host_audio COMMAND phase5_integration_tests)
set_tests_properties(phase5_parasite_audio PROPERTIES LABELS "phase5_audio")
set_tests_properties(phase5_host_ecology PROPERTIES LABELS "phase5_ecology")
set_tests_properties(phase5_host_audio PROPERTIES LABELS "phase5_audio;phase5_ecology")
add_test(NAME phase6_decomposer_audio COMMAND decomposer_processor_tests)
add_test(NAME phase6_repetition_ecology COMMAND phase6_ecology_tests)
add_test(NAME phase6_repetition_audio COMMAND phase6_integration_tests)
set_tests_properties(phase6_decomposer_audio PROPERTIES LABELS "phase6_audio")
set_tests_properties(phase6_repetition_ecology PROPERTIES LABELS "phase6_ecology")
set_tests_properties(phase6_repetition_audio PROPERTIES LABELS "phase6_audio;phase6_ecology")
add_test(NAME phase3_closed_loop_render
    COMMAND phase3_render_cli
            "${CMAKE_BINARY_DIR}/phase3-ctest-dry.wav"
            "${CMAKE_BINARY_DIR}/phase3-ctest-processed.wav")
set_tests_properties(phase3_closed_loop_render PROPERTIES LABELS "phase3_audio;phase3_ecology")
add_test(NAME phase4_closed_loop_render
    COMMAND phase4_render_cli
            "${CMAKE_BINARY_DIR}/phase4-ctest-dry.wav"
            "${CMAKE_BINARY_DIR}/phase4-ctest-processed.wav")
set_tests_properties(phase4_closed_loop_render PROPERTIES LABELS "phase4_audio;phase4_ecology")
