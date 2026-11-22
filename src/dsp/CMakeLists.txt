# lowchug_dsp: the plugin-independent DSP core. Depends only on the C++
# standard library and the (header-only, MIT) Signalsmith libraries.

add_library(lowchug_dsp STATIC
    Detuner.cpp
    PitchDetector.cpp
    engines/GranularEngine.cpp
    engines/PhaseVocoderEngine.cpp
    engines/SignalsmithEngine.cpp)

target_include_directories(lowchug_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lowchug_dsp PRIVATE signalsmith-stretch signalsmith-linear)
target_compile_features(lowchug_dsp PUBLIC cxx_std_20)

# The plugin is a shared library, so the DSP code linked into it must be
# position-independent.
set_target_properties(lowchug_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(lowchug_dsp PRIVATE /W4)
else()
    target_compile_options(lowchug_dsp PRIVATE -Wall -Wextra -Wshadow)
endif()
