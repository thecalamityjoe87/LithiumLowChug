add_executable(lowchug-engine-eval main.cpp)
target_link_libraries(lowchug-engine-eval PRIVATE lowchug_dsp signalsmith-linear)

if(NOT MSVC)
    target_compile_options(lowchug-engine-eval PRIVATE -Wall -Wextra)
endif()
