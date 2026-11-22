# DSP tests: plain C++, no JUCE, so they build and run in seconds.
add_executable(lowchug-dsp-tests DspTests.cpp)
target_link_libraries(lowchug-dsp-tests PRIVATE lowchug_dsp signalsmith-linear)
add_test(NAME dsp COMMAND lowchug-dsp-tests)

# Plugin tests: instantiate the real processor through JUCE.
if(LOWCHUG_BUILD_PLUGIN)
    add_executable(lowchug-plugin-tests PluginTests.cpp)
    target_link_libraries(lowchug-plugin-tests PRIVATE
        LithiumLowChug # the plugin's shared code (processor, parameters, JUCE modules)
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    # The plugin target compiles the JUCE modules privately; borrow its include
    # paths and JucePlugin_* definitions so the tests see exactly the same
    # configuration without compiling JUCE a second time.
    target_include_directories(lowchug-plugin-tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        $<TARGET_PROPERTY:LithiumLowChug,INCLUDE_DIRECTORIES>)
    target_compile_definitions(lowchug-plugin-tests PRIVATE
        $<TARGET_PROPERTY:LithiumLowChug,COMPILE_DEFINITIONS>)
    add_test(NAME plugin COMMAND lowchug-plugin-tests)
endif()
