# tools/integration-test defines a script for running a series of scripts that exercise
# the SDK via the repl binary, validating that the expected set of events are produced.
#
# When the CMake build is configured with DD_ENABLE_INTEGRATION_TEST enabled, this
# file is included, and it sets up targets so that we can easily invoke the integration
# test suite with:
#
# > cmake --build build --target integration-test
#

# find_package(Python3 ...) will preferentially resolve the system Python interpreter
# before checking the PATH: on MacOS, this gives us an ancient version of pip that can't
# install modern packages - so perform a manual PATH lookup first
if(NOT INTEGRATION_TEST_SYSTEM_PYTHON)
    find_program(INTEGRATION_TEST_SYSTEM_PYTHON python3)
endif()

# If that didn't work, fall back to find_package()
if(NOT INTEGRATION_TEST_SYSTEM_PYTHON)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(INTEGRATION_TEST_SYSTEM_PYTHON ${Python3_EXECUTABLE})
    endif()
endif()

# Mandate Python 3.10 or higher, and print a helpful error on failure
if(NOT INTEGRATION_TEST_SYSTEM_PYTHON)
    message(FATAL_ERROR "Python 3.10+ is required for integration tests. Please install it, set INTEGRATION_TEST_SYSTEM_PYTHON, or re-run with DD_ENABLE_INTEGRATION_TEST=OFF")
endif()
execute_process(
    COMMAND ${INTEGRATION_TEST_SYSTEM_PYTHON} -c "import sys; assert sys.version_info >= (3,10)"
    RESULT_VARIABLE PYTHON_VERSION_OK
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT PYTHON_VERSION_OK EQUAL 0)
    message(FATAL_ERROR "Found ${INTEGRATION_TEST_SYSTEM_PYTHON}, but integration tests require Python 3.10+. Please install it, set INTEGRATION_TEST_SYSTEM_PYTHON, or re-run with DD_ENABLE_INTEGRATION_TEST=OFF")
endif()

# We'll use that system Python3 interpreter to create a .venv directory within
# tools/integration-test/, and we'll use the venv's Python interpreter thereafter
set(INTEGRATION_TEST_VENV_DIR "${DD_SDK_ROOT_DIR}/tools/integration-test/.venv")
if(WIN32)
    set(INTEGRATION_TEST_VENV_PYTHON "${INTEGRATION_TEST_VENV_DIR}/Scripts/python.exe")
else()
    set(INTEGRATION_TEST_VENV_PYTHON "${INTEGRATION_TEST_VENV_DIR}/bin/python")
endif()

# Register a custom command that initializes the .venv directory when it doesn't exist
add_custom_command(
    OUTPUT ${INTEGRATION_TEST_VENV_PYTHON}
    COMMAND ${INTEGRATION_TEST_SYSTEM_PYTHON} -m venv ${INTEGRATION_TEST_VENV_DIR}
    COMMENT "Creating Python virtual environment at ${INTEGRATION_TEST_VENV_DIR}"
)

# Define a integration-test-venv target that requires a .venv and ensures it has the
# latest dependencies installed as specified in requirements.txt
add_custom_target(integration-test-venv
    DEPENDS ${INTEGRATION_TEST_VENV_PYTHON}
    COMMAND ${INTEGRATION_TEST_VENV_PYTHON} -m pip install -r ${DD_SDK_ROOT_DIR}/tools/integration-test/requirements.txt
    COMMENT "Installing Python dependencies for tools/integration-test"
)


# Define a integration-test target that requires a .venv with all dependencies, and
# that uses the venv's Python interpreter to run tools/integration-test/main.py,
# forwarding command-line arguments
add_custom_target(integration-test
    DEPENDS integration-test-venv
    COMMAND ${INTEGRATION_TEST_VENV_PYTHON} ${DD_SDK_ROOT_DIR}/tools/integration-test/main.py
    COMMENT "Running: tools/integration-test/main.py"
)
