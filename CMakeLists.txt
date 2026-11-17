cmake_minimum_required (VERSION 3.22)

# Alvo mínimo do macOS: 11.0 (Big Sur), para rodar também no MacBook de 2010.
# Precisa ser definido antes de project().
if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set (CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project (ZStudio VERSION 0.2.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_property (GLOBAL PROPERTY USE_FOLDERS YES)

# Motor de áudio: Tracktion Engine (traz o JUCE como submódulo)
set (TE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/tracktion_engine")

if (NOT EXISTS "${TE_DIR}/modules/juce/CMakeLists.txt")
    message (FATAL_ERROR "Submódulos ausentes. Rode: git submodule update --init --recursive")
endif()

add_subdirectory ("${TE_DIR}/modules/juce" juce_build)
add_subdirectory ("${TE_DIR}/modules" tracktion_build)

# Motor de desenho de partituras (Verovio)
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/verovio/src")
    message (FATAL_ERROR "Submódulo do Verovio ausente. Rode: git submodule update --init --recursive")
endif()
include (cmake/Verovio.cmake)

set (SCORE_SOURCES
    Source/Score/ScoreEngine.cpp
    Source/Score/MusicXml.cpp
    Source/Score/ScoreRenderer.cpp)

juce_add_gui_app (ZStudio
    PRODUCT_NAME "Z-Studio"
    COMPANY_NAME "Eliseu F. Guedes"
    BUNDLE_ID "com.eliseufguedes.zstudio"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "O Z-Studio usa a entrada de áudio para gravar."
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    PLUGINHOST_AU TRUE
    DOCUMENT_EXTENSIONS zstudio)

juce_generate_juce_header (ZStudio)

target_sources (ZStudio PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/ArrangeView.cpp
    Source/ScoreView.cpp
    Source/PianoRoll.cpp
    ${SCORE_SOURCES})

# Fontes do Verovio embutidas (extraídas na primeira execução)
juce_add_binary_data (ZStudioData
    HEADER_NAME ZeufgBinaryData.h
    NAMESPACE ZeufgData
    SOURCES "${VRV_DATA_ZIP}")
add_dependencies (ZStudioData verovio_data)

target_compile_definitions (ZStudio PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ZStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ZStudio,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    $<$<PLATFORM_ID:Linux>:JUCE_PLUGINHOST_LADSPA=1>
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_ASIO=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_link_libraries (ZStudio PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    verovio_static
    ZStudioData
    juce::juce_recommended_config_flags)

set_target_properties (ZStudio PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

if (APPLE)
    target_compile_options (ZStudio PRIVATE "-fno-aligned-allocation")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options (ZStudio PRIVATE "-Wno-unknown-pragmas")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries (ZStudio PRIVATE "-latomic")
endif()

#==============================================================================
# Ferramenta de teste do motor de partitura (opcional)
option (ZS_BUILD_TOOLS "Compila ferramentas de teste" OFF)

if (ZS_BUILD_TOOLS)
    juce_add_console_app (score_cli PRODUCT_NAME "score_cli")
    target_sources (score_cli PRIVATE tools/score_cli.cpp ${SCORE_SOURCES})
    target_compile_definitions (score_cli PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries (score_cli PRIVATE juce::juce_gui_basics juce::juce_audio_basics verovio_static
                           juce::juce_recommended_config_flags)
    juce_generate_juce_header (score_cli)
endif()
