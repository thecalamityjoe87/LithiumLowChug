set(lowchug_formats VST3 LV2 Standalone)
if(APPLE)
    list(APPEND lowchug_formats AU)
endif()

# The app icon (drawn by tools/icongen, see scripts/make-icons.sh): the
# standalone app's icon on Windows and macOS, and the AU's on macOS. Only
# passed when the files exist, so a checkout without them still builds.
set(lowchug_icons)
set(lowchug_icon_dir ${PROJECT_SOURCE_DIR}/assets/icon)
if(EXISTS ${lowchug_icon_dir}/icon-1024.png AND EXISTS ${lowchug_icon_dir}/icon-256.png)
    set(lowchug_icons
        ICON_BIG   ${lowchug_icon_dir}/icon-1024.png
        ICON_SMALL ${lowchug_icon_dir}/icon-256.png)
endif()

juce_add_plugin(LithiumLowChug
    PRODUCT_NAME                "${LOWCHUG_PRODUCT_NAME}"
    DESCRIPTION                 "${LOWCHUG_DESCRIPTION}"
    COMPANY_NAME                "${LOWCHUG_COMPANY_NAME}"
    COMPANY_COPYRIGHT           "Copyright (C) 2026 ${LOWCHUG_AUTHOR}"
    COMPANY_WEBSITE             "${LOWCHUG_WEBSITE}"
    BUNDLE_ID                   "${LOWCHUG_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    "${LOWCHUG_MANUFACTURER_CODE}"
    PLUGIN_CODE                 "${LOWCHUG_PLUGIN_CODE}"
    FORMATS                     ${lowchug_formats}
    ${lowchug_icons}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${LOWCHUG_COPY_PLUGINS}
    VST3_CATEGORIES             Fx "Pitch Shift"
    AU_MAIN_TYPE                kAudioUnitType_Effect
    LV2URI                      "${LOWCHUG_LV2_URI}"
    LV2_PLUGIN_CLASS            PitchPlugin)

target_sources(LithiumLowChug PRIVATE
    Parameters.cpp
    PluginProcessor.cpp
    Presets.cpp
    StandaloneAudio.cpp
    Tunings.cpp
    ../ui/AboutPanel.cpp
    ../ui/Controls.cpp
    ../ui/HeaderBar.cpp
    ../ui/LithiumLookAndFeel.cpp
    ../ui/MainView.cpp
    ../ui/PluginEditor.cpp
    ../ui/PresetBar.cpp
    ../ui/ReadoutPanel.cpp
    ../ui/ShiftAnalyzer.cpp
    ../ui/TuningController.cpp
    ../ui/TunerView.cpp
    ../ui/TuningPanel.cpp)

# Every source includes headers relative to src/ ("plugin/...", "ui/...", "dsp/...").
target_include_directories(LithiumLowChug PRIVATE ${PROJECT_SOURCE_DIR}/src)

# The Lithium family fonts (SIL Open Font License, see assets/fonts/OFL-*.txt),
# compiled into the plugin so it looks the same on every machine without
# installing anything. JUCE turns each file into a byte array in the
# LithiumFonts namespace (LithiumFonts::BarlowRegular_ttf etc.).
juce_add_binary_data(LithiumFonts
    NAMESPACE   LithiumFonts
    HEADER_NAME LithiumFonts.h
    SOURCES
        ${PROJECT_SOURCE_DIR}/assets/fonts/Barlow-Regular.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/Barlow-Medium.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/Barlow-SemiBold.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/BarlowCondensed-Medium.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/BarlowCondensed-SemiBold.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/BarlowCondensed-Bold.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/IBMPlexMono-Regular.ttf
        ${PROJECT_SOURCE_DIR}/assets/fonts/IBMPlexMono-Medium.ttf)
set_target_properties(LithiumFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(LithiumLowChug PUBLIC
    # Shown in the About panel.
    LOWCHUG_AUTHOR_STRING="${LOWCHUG_AUTHOR}"
    # No web browser or network code in the plugin.
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LithiumLowChug
    PRIVATE
        lowchug_dsp
        LithiumFonts
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
