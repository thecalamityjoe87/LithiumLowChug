# Writes the app icon and the installers' artwork (assets/icon/) from the logo
# code (src/ui/Logo.cpp). Like Lithium Drums' LithiumIconGen, its output is
# committed, so the build only compiles the tool; run scripts/make-icons.sh by
# hand after changing the logo.
juce_add_console_app(lowchug-icongen PRODUCT_NAME "lowchug-icongen")
target_sources(lowchug-icongen PRIVATE
    IconGenerator.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/Logo.cpp)
target_include_directories(lowchug-icongen PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(lowchug-icongen PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(lowchug-icongen PRIVATE
    LithiumFonts
    juce::juce_graphics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
