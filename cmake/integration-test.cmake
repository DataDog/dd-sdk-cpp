# tools/integration-test defines a script for running a series of scripts that exercise
# the SDK via the repl binary, validating that the expected set of events are produced.
#
# When the CMake build is configured with DD_ENABLE_INTEGRATION_TEST enabled, this
# file is included, and it sets up targets so that we can easily invoke the integration
# test suite with:
#
# > cmake --build build --target integration-test
#

include("${DD_SDK_ROOT_DIR}/cmake/python.cmake")

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
    COMMAND ${DD_SYSTEM_PYTHON} -m venv ${INTEGRATION_TEST_VENV_DIR}
    COMMENT "Creating Python virtual environment at ${INTEGRATION_TEST_VENV_DIR}"
)

# Define a integration-test-venv target that requires a .venv and ensures it has the
# latest dependencies installed as specified in pyproject.toml
add_custom_target(integration-test-venv
    DEPENDS ${INTEGRATION_TEST_VENV_PYTHON}
    COMMAND ${INTEGRATION_TEST_VENV_PYTHON} -m pip install -e "${DD_SDK_ROOT_DIR}[integration-test]"
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
