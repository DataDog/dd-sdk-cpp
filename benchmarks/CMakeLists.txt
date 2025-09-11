# benchmark program
add_executable(dd_native_benchmark
    common/arg.cpp
    common/benchmark.cpp
    common/config.cpp
    common/global.cpp
    common/os.cpp
    common/server.cpp
    benchmark_logging.cpp
    benchmark_startup.cpp
    main.cpp
)
target_enable_strict_warnings(dd_native_benchmark)
target_enable_clang_tidy(dd_native_benchmark)
target_link_libraries(dd_native_benchmark PRIVATE dd_native_c_api dd_native_cpp_api)
target_include_directories(dd_native_benchmark PRIVATE .)

# When configured with --intake=mock, the benchmark program uses the system Python
# interpreter to run an HTTP server process: this requires common/server.py to be
# distributed alongside the benchmark binary
add_custom_command(TARGET dd_native_benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/common/server.py"
            "$<TARGET_FILE_DIR:dd_native_benchmark>/"
    COMMENT "Copying server.py to $<TARGET_FILE_DIR:dd_native_benchmark>/"
)
