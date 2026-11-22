set(lowchug_formats VST3 LV2 Standalone)
if(APPLE)
    list(APPEND lowchug_formats AU)
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
    Tunings.cpp)

target_compile_definitions(LithiumLowChug PUBLIC
    # No web browser or network code in the plugin.
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LithiumLowChug
    PRIVATE
        lowchug_dsp
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
