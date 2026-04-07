# Locates a system Python 3.12+ interpreter and stores it in DD_SYSTEM_PYTHON.
# Include this file from any cmake module that needs to invoke Python. It is
# safe to include multiple times; the lookup runs only once per configure.
# To override the resolved interpreter, set DD_SYSTEM_PYTHON in the CMake cache.

# Skip if a prior include (or the user) already resolved DD_SYSTEM_PYTHON
if(DD_SYSTEM_PYTHON)
    return()
endif()

# Allow the DD_SYSTEM_PYTHON environment variable to override the PATH lookup
if(DEFINED ENV{DD_SYSTEM_PYTHON})
    set(DD_SYSTEM_PYTHON $ENV{DD_SYSTEM_PYTHON} CACHE FILEPATH "Path to system Python 3.12+ interpreter")
endif()

# find_package(Python3 ...) will preferentially resolve the system Python interpreter
# before checking the PATH: on MacOS, this gives us an ancient version of pip that can't
# install modern packages - so perform a manual PATH lookup first
if(NOT DD_SYSTEM_PYTHON)
    find_program(DD_SYSTEM_PYTHON python3)
endif()

# If that didn't work, fall back to find_package()
if(NOT DD_SYSTEM_PYTHON)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(DD_SYSTEM_PYTHON ${Python3_EXECUTABLE})
    endif()
endif()

# Mandate Python 3.12 or higher, and print a helpful error on failure
if(NOT DD_SYSTEM_PYTHON)
    message(FATAL_ERROR "Python 3.12+ is required. Please install it or set DD_SYSTEM_PYTHON.")
endif()
execute_process(
    COMMAND ${DD_SYSTEM_PYTHON} -c "import sys; assert sys.version_info >= (3,12)"
    RESULT_VARIABLE _python_version_ok
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT _python_version_ok EQUAL 0)
    message(FATAL_ERROR "Found ${DD_SYSTEM_PYTHON}, but Python 3.12+ is required. Please install it or set DD_SYSTEM_PYTHON.")
endif()
