# The CPU profiler (see main.cpp), built twice:
#   lowchug-profile         against the shipping DSP library;
#   lowchug-profile-stages  against its own copy of the DSP sources, compiled
#                           with LOWCHUG_PROFILE so the stage timers in
#                           src/dsp/Profiling.h are switched on.

add_executable(lowchug-profile main.cpp)
target_link_libraries(lowchug-profile PRIVATE lowchug_dsp)

add_library(lowchug_dsp_profiled STATIC EXCLUDE_FROM_ALL
    ${PROJECT_SOURCE_DIR}/src/dsp/Detuner.cpp
    ${PROJECT_SOURCE_DIR}/src/dsp/engines/PhaseVocoderEngine.cpp)
target_include_directories(lowchug_dsp_profiled PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lowchug_dsp_profiled PRIVATE signalsmith-linear)
target_compile_definitions(lowchug_dsp_profiled PUBLIC LOWCHUG_PROFILE=1)
target_compile_features(lowchug_dsp_profiled PUBLIC cxx_std_20)

add_executable(lowchug-profile-stages main.cpp)
target_link_libraries(lowchug-profile-stages PRIVATE lowchug_dsp_profiled)

if(NOT MSVC)
    target_compile_options(lowchug-profile PRIVATE -Wall -Wextra)
    target_compile_options(lowchug-profile-stages PRIVATE -Wall -Wextra)
endif()
