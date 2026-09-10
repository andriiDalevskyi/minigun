cmake_minimum_required(VERSION 3.22)

project(Minigun VERSION 0.3.6)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(external/JUCE)

juce_add_plugin(Minigun
    COMPANY_NAME "Dallas Audio"
    BUNDLE_ID "com.dallasaudio.minigun"
    PLUGIN_MANUFACTURER_CODE Dlas
    PLUGIN_CODE Mngn
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon_512.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon_64.png"
    PRODUCT_NAME "Minigun")

juce_generate_juce_header(Minigun)

file(GLOB_RECURSE MINIGUN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

target_sources(Minigun PRIVATE ${MINIGUN_SOURCES})

target_include_directories(Minigun PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(Minigun
    PUBLIC
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_OGGVORBIS=1
        JUCE_USE_FLAC=1
        JUCE_USE_WINDOWS_MEDIA_FORMAT=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Minigun
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
