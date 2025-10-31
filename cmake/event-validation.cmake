# tools/validate-event-types defines a script for validating that the RUM event payloads
# used in unit tests conform to the rum-events-format schema.
#
# When the CMake build is configured with DD_ENABLE_EVENT_VALIDATION enabled, this
# file is included, and it sets up targets so that we can easily invoke the event
# validation script with:
#
# > cmake --build build --target validate-event-types
#
# To validate against a specific revision, pass a branch name or commit SHA when
# configuring CMake, e.g.:
#
# > cmake \
#    -DDD_ENABLE_EVENT_VALIDATION=ON \
#    -DEVENT_VALIDATION_COMMIT_REF=jdoe/RUM-1234/my-branch \
#    . -B build && \
#   cmake --build build --target validate-event-types

# We can't forward arbitrary command-line arguments when we invoke our custom target via
# cmake --build, so we need to accept the target revision (passed as the first
# positional arg to validate-event-types/main.py) as a CMake cache variable
set(EVENT_VALIDATION_COMMIT_REF "" CACHE STRING "Revision of rum-events-format to validate JSON events against (defaults to latest)")

# find_package(Python3 ...) will preferentially resolve the system Python interpreter
# before checking the PATH: on MacOS, this gives us an ancient version of pip that can't
# install modern packages - so perform a manual PATH lookup first
if(NOT EVENT_VALIDATION_SYSTEM_PYTHON)
    find_program(EVENT_VALIDATION_SYSTEM_PYTHON python3)
endif()

# If that didn't work, fall back to find_package()
if(NOT EVENT_VALIDATION_SYSTEM_PYTHON)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(EVENT_VALIDATION_SYSTEM_PYTHON ${Python3_EXECUTABLE})
    endif()
endif()

# Mandate Python 3.10 or higher, and print a helpful error on failure
if(NOT EVENT_VALIDATION_SYSTEM_PYTHON)
    message(FATAL_ERROR "Python 3.10+ is required for JSON event validation. Please install it, set EVENT_VALIDATION_SYSTEM_PYTHON, or re-run with DD_ENABLE_EVENT_VALIDATION=OFF")
endif()
execute_process(
    COMMAND ${EVENT_VALIDATION_SYSTEM_PYTHON} -c "import sys; assert sys.version_info >= (3,10)"
    RESULT_VARIABLE PYTHON_VERSION_OK
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT PYTHON_VERSION_OK EQUAL 0)
    message(FATAL_ERROR "Found ${EVENT_VALIDATION_SYSTEM_PYTHON}, but event validation requires Python 3.10+. Please install it, set EVENT_VALIDATION_SYSTEM_PYTHON, or re-run with DD_ENABLE_EVENT_VALIDATION=OFF")
endif()

# We'll use that system Python3 interpreter to create a .venv directory within
# tools/validate-event-types/, and we'll use the venv's Python interpreter thereafter
set(EVENT_VALIDATION_VENV_DIR "${DD_SDK_ROOT_DIR}/tools/validate-event-types/.venv")
if(WIN32)
    set(EVENT_VALIDATION_VENV_PYTHON "${EVENT_VALIDATION_VENV_DIR}/Scripts/python.exe")
else()
    set(EVENT_VALIDATION_VENV_PYTHON "${EVENT_VALIDATION_VENV_DIR}/bin/python")
endif()

# Register a custom command that initializes the .venv directory when it doesn't exist
add_custom_command(
    OUTPUT ${EVENT_VALIDATION_VENV_PYTHON}
    COMMAND ${EVENT_VALIDATION_SYSTEM_PYTHON} -m venv ${EVENT_VALIDATION_VENV_DIR}
    COMMENT "Creating Python virtual environment at ${EVENT_VALIDATION_VENV_DIR}"
)

# Define a validate-event-types-venv target that requires a .venv and ensures it has the
# latest dependencies installed as specified in requirements.txt
add_custom_target(validate-event-types-venv
    DEPENDS ${EVENT_VALIDATION_VENV_PYTHON}
    COMMAND ${EVENT_VALIDATION_VENV_PYTHON} -m pip install -r ${DD_SDK_ROOT_DIR}/tools/validate-event-types/requirements.txt
    COMMENT "Installing Python dependencies for tools/validate-event-types"
)


# Define a validate-event-types target that requires a .venv with all dependencies, and
# that uses the venv's Python interpreter to run tools/validate-event-types/main.py,
# forwarding command-line arguments
add_custom_target(validate-event-types
    DEPENDS validate-event-types-venv
    COMMAND ${EVENT_VALIDATION_VENV_PYTHON} ${DD_SDK_ROOT_DIR}/tools/validate-event-types/main.py ${EVENT_VALIDATION_COMMIT_REF}
    COMMENT "Running: tools/validate-event-types/main.py ${EVENT_VALIDATION_COMMIT_REF}"
)
