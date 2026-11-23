# DSP tests: plain C++, no JUCE, so they build and run in seconds.
add_executable(lowchug-dsp-tests DspTests.cpp)
target_link_libraries(lowchug-dsp-tests PRIVATE lowchug_dsp signalsmith-linear)
add_test(NAME dsp COMMAND lowchug-dsp-tests)

# Stress tests on a copy of the DSP code compiled with the standard library's
# bounds checks switched on, so an out-of-range read stops the test even in a
# release build (see CheckedDspTests.cpp). Each define only affects its own
# standard library and is ignored by the others.
add_executable(lowchug-dsp-checked-tests
    CheckedDspTests.cpp
    ${PROJECT_SOURCE_DIR}/src/dsp/Detuner.cpp
    ${PROJECT_SOURCE_DIR}/src/dsp/engines/PhaseVocoderEngine.cpp)
target_include_directories(lowchug-dsp-checked-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lowchug-dsp-checked-tests PRIVATE signalsmith-linear)
target_compile_definitions(lowchug-dsp-checked-tests PRIVATE
    _GLIBCXX_ASSERTIONS                                     # libstdc++ (GCC)
    _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE) # libc++ (Clang, macOS)
add_test(NAME dsp-checked COMMAND lowchug-dsp-checked-tests)

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

    # The Linux installer's logic (tools/installer/Installation.cpp): paths,
    # parts, install / update / remove in a scratch home folder.
    if(UNIX AND NOT APPLE)
        juce_add_console_app(lowchug-installer-tests PRODUCT_NAME "lowchug-installer-tests")
        target_sources(lowchug-installer-tests PRIVATE
            InstallerTests.cpp
            ${PROJECT_SOURCE_DIR}/tools/installer/Installation.cpp)
        target_compile_definitions(lowchug-installer-tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(lowchug-installer-tests PRIVATE
            juce::juce_core
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
        add_test(NAME installer COMMAND lowchug-installer-tests)
    endif()
endif()
