# The graphical installer in the Linux download ("Install Lithium LowChug"),
# the same as Lithium Drums' (see InstallerMain.cpp). Windows and macOS use
# their own installers (packaging/). Only added on Linux (see the top-level
# CMakeLists.txt).
juce_add_gui_app(LithiumLowChugInstaller
    PRODUCT_NAME "Install Lithium LowChug"
    COMPANY_NAME "${LOWCHUG_COMPANY_NAME}"
    BUNDLE_ID    "${LOWCHUG_BUNDLE_ID}.installer"
    VERSION      "${PROJECT_VERSION}")
target_sources(LithiumLowChugInstaller PRIVATE
    InstallerMain.cpp
    Installation.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/Logo.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/LithiumLookAndFeel.cpp)
target_include_directories(LithiumLowChugInstaller PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(LithiumLowChugInstaller PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    LOWCHUG_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(LithiumLowChugInstaller PRIVATE
    LithiumFonts
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
