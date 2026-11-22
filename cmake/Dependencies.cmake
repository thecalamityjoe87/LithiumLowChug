# Third-party code, fetched at configure time and pinned to exact versions so
# every build uses the same sources.
#
# To build offline, point FetchContent at a local checkout, e.g.
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE

include(FetchContent)

# Signalsmith Linear: FFT/STFT used by Signalsmith Stretch (and by our own
# phase vocoder and the evaluation harness). MIT licensed. Declared before
# Stretch so Stretch's own CMakeLists uses this pinned version.
FetchContent_Declare(signalsmith-linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG        0.6.6
    GIT_SHALLOW    TRUE
    SYSTEM)

# Signalsmith Stretch: polyphonic pitch/time engine. MIT licensed.
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    TRUE
    SYSTEM)

FetchContent_MakeAvailable(signalsmith-linear signalsmith-stretch)

if(LOWCHUG_BUILD_PLUGIN)
    # JUCE 8 and later is dual-licensed: AGPLv3, or a commercial JUCE licence.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        SYSTEM)
    FetchContent_MakeAvailable(JUCE)
endif()
