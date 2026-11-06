# tools/integration-test defines a script for running a series of scripts that exercise
# the SDK via the repl binary, validating that the expected set of events are produced.
#
# When the CMake build is configured with DD_ENABLE_INTEGRATION_TEST enabled, this
# file is included, and it sets up targets so that we can easily invoke the integration
# test suite with:
#
# > cmake --build build --target integration-test
#

# The integration tests run the repl example program, so they require examples
if(NOT DD_BUILD_EXAMPLES)
    message(FATAL_ERROR "DD_ENABLE_INTEGRATION_TEST requires DD_BUILD_EXAMPLES")
endif()

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

# Install the integration test dependencies into the .venv, recording success in a stamp
# file. pip only runs again if the .venv is recreated or pyproject.toml changes; changes
# to the tools' own source take effect without reinstalling, since pip installs them in
# editable mode.
set(INTEGRATION_TEST_VENV_STAMP "${INTEGRATION_TEST_VENV_DIR}/.deps-installed.stamp")
add_custom_command(
    OUTPUT ${INTEGRATION_TEST_VENV_STAMP}
    COMMAND ${INTEGRATION_TEST_VENV_PYTHON} -m pip install -e "${DD_SDK_ROOT_DIR}[integration-test]"
    COMMAND ${CMAKE_COMMAND} -E touch ${INTEGRATION_TEST_VENV_STAMP}
    DEPENDS ${INTEGRATION_TEST_VENV_PYTHON} ${DD_SDK_ROOT_DIR}/pyproject.toml
    COMMENT "Installing Python dependencies for tools/integration-test"
)
add_custom_target(integration-test-venv DEPENDS ${INTEGRATION_TEST_VENV_STAMP})

# The command that runs the integration test suite against the repl binary from this
# build. check-all runs the same command after the unit tests.
set(INTEGRATION_TEST_COMMAND
    ${INTEGRATION_TEST_VENV_PYTHON} ${DD_SDK_ROOT_DIR}/tools/integration-test/main.py
    --crash-mode ${DD_CRASH_MODE}
    --repl $<TARGET_FILE:repl>
)

# Sanitizers intercept some deliberate crashes before the SDK's crash handler sees them,
# so tell the runner to skip the tests that depend on those crashes
if(DD_ENABLE_SANITIZERS)
    list(APPEND INTEGRATION_TEST_COMMAND --sanitized)
endif()

# Define an integration-test target that builds repl, ensures the .venv has all
# dependencies, and then runs the test suite
add_custom_target(integration-test
    COMMAND ${INTEGRATION_TEST_COMMAND}
    COMMENT "Running: tools/integration-test/main.py"
    VERBATIM
)
add_dependencies(integration-test repl integration-test-venv)
