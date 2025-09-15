#!/usr/bin/env bash
# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0. This product includes software developed at Datadog
# (https://www.datadoghq.com/). Copyright 2024-Present Datadog, Inc.  

# Helper script for running build tasks locally in a way that mirrors CI.
#
# To build and run a Linux clang20 container, in an interactive shell:
#  ./docker-ci.sh shell
#
# To run the build validation task with a specific compiler toolchain:
#  ./docker-ci.sh --toolchain gcc13 run build
#
# To grab a command you can use to replicate a specific CI job on your machine:
#  ./docker-ci.sh job unit-test | pbcopy
#
# To list all CI jobs defined in .gitlab-ci.yml:
#  ./docker-ci.sh list
#
# Options:
#  --toolchain <toolchain>: Specifies which compiler to use; see ci/linux/Dockerfile.
#  --skip-build: If set, don't run a docker build before running containers.
#  --clean: If set, your build directory will be deleted prior to running containers.
#  --exit: If set, the container will exit after completing the 'run' command.
#
# Docker containers will have your working directory mounted as CONTAINER_CWD, so any
# source changes you make locally will be reflected in the container environment.
# Containers will also inherit any existing 'build' directory: watch out for incremental
# build state if not using --clean.
set -e

CONTAINER_CWD="/go/src/github.com/DataDog/dd-sdk-cpp"

# Default values substituted in .gitlab-ci.yml scripts; exported for envsubst
export BUILD_CONFIGURATION="${BUILD_CONFIGURATION:-Debug}"
export BUILD_SHARED_LIBS="${BUILD_SHARED_LIBS:OFF}"
export USE_SYSTEM_LIBCURL="${USE_SYSTEM_LIBCURL:ON}"

# Computes the Docker image name to use locally for a given --target
image_tag() {
    TARGET="$1"
    echo "ci/dd-sdk-cpp/$TARGET"
}

# Builds a local Docker image from ci/linux/Dockerfile for the chosen target
build_image() {
    TARGET="$1"
    docker buildx build \
        --target "$TARGET" \
        --tag $(image_tag "$TARGET") \
        --label target=build \
        ./ci/linux
}

# Runs a Docker container from a locally-build image, executing the given command
run_container() {
    TARGET="$1"
    ENTRYPOINT="$2"
    shift 2;
    docker run -it --rm \
        -v "$(pwd):$CONTAINER_CWD" \
        -w $CONTAINER_CWD \
        $(image_tag "$TARGET") \
        "$ENTRYPOINT" "$@"
}

# Verifies that yq is installed, printing an error message and exiting if not
require_yq() {
    if ! command -v yq &> /dev/null; then
        echo "ERROR: yq is required but not found in PATH" >&2
        echo "Try 'brew install yq', or see https://github.com/mikefarah/yq" >&2
        exit 1
    fi
}

# Uses yq to resolve a CI job name (allowing 'foo' in place of 'foo-linux')
resolve_job_name() {
    NAME_OR_PREFIX="$1"
    HAS_NAME_OR_PREFIX=$(yq e "has(\"$NAME_OR_PREFIX\")" .gitlab-ci.yml)
    if [ "$HAS_NAME_OR_PREFIX" == "true" ]; then
        echo "$NAME_OR_PREFIX"
    else
        NAME="$NAME_OR_PREFIX-linux"
        HAS_NAME=$(yq e "has(\"$NAME\")" .gitlab-ci.yml)
        if [ "$HAS_NAME" != "true" ]; then
            echo "ERROR: .gitlab-ci.yml defines no job named '$NAME_OR_PREFIX' (or '$NAME')" >&2
            exit 1
        fi
        echo "$NAME"
    fi
}

# Returns a one-line shell command that will run the commands for given CI job,
# substituting supported environment variables
retrieve_args() {
    JOB_NAME=$(resolve_job_name "$1")
    yq e ".$JOB_NAME.script | join(\" && \") | sub(\"\\n\"; \"\"; \"g\")" .gitlab-ci.yml | envsubst
}

# Parse command-line options
TOOLCHAIN_TARGET="clang20"
SKIP_DOCKER_BUILD="0"
CLEAN_BUILD_DIR="0"
RUN_IS_INTERACTIVE="1"
POSITIONAL_ARGS=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --toolchain)
            TOOLCHAIN_TARGET="$2"
            shift 2
            ;;
        --skip-build)
            SKIP_DOCKER_BUILD="1"
            shift
            ;;
        --clean)
            CLEAN_BUILD_DIR="1"
            shift
            ;;
        --exit)
            RUN_IS_INTERACTIVE="0"
            shift
            ;;
        -*)
            echo "ERROR: Unknown option '$1'" >&2
            exit 1
            ;;
        *)
            POSITIONAL_ARGS+=("$1")
            shift
            ;;
    esac
done
set -- "${POSITIONAL_ARGS[@]}"

# Handle commands
COMMAND="$1"
case "$COMMAND" in
    build)
        # Build a local docker image and exit
        build_image "$TOOLCHAIN_TARGET"
        ;;
    shell)
        # Optionally build, then docker run --rm -it /bin/bash
        [ "$SKIP_DOCKER_BUILD" != "0" ] || build_image "$TOOLCHAIN_TARGET"
        [ "$CLEAN_BUILD_DIR" == "0" ] || rm -rf build
        run_container "$TOOLCHAIN_TARGET" /bin/bash
        ;;
    list)
        # Print a list of CI jobs that can be run with this script
        require_yq
        JOBS=$(yq e 'del(.stages, .variables, .build-toolchain-image) | keys | .[]' .gitlab-ci.yml)
        echo "Available CI jobs:"
        echo ""
        while IFS= read -r JOB; do
            echo "- $JOB"
        done <<< "$JOBS"
        echo ""
        echo "Use './docker-ci.sh run <job> [--clean] [--exit]' to run in Docker."
        echo "Use './docker-ci.sh job <job>' to print shell commands."
        ;;
    job)
        # Print the commands used to run a specific CI job
        JOB="$2"
        if [ "$JOB" == "" ]; then
            echo "ERROR: 'job' must be followed by a CI job to run, e.g. ./docker-ci job unit-test" >&2
            exit 1
        fi
        require_yq
        retrieve_args "$JOB"
        ;;
    run)
        JOB="$2"
        if [ "$JOB" == "" ]; then
            echo "ERROR: 'run' must be followed by a CI job to run, e.g. ./docker-ci run unit-test" >&2
            exit 1
        fi
        require_yq
        COMMANDS=$(retrieve_args "$JOB")
        [ "$SKIP_DOCKER_BUILD" != "0" ] || build_image "$TOOLCHAIN_TARGET"
        [ "$CLEAN_BUILD_DIR" == "0" ] || rm -rf build
        echo "$COMMANDS"
        if [ "$RUN_IS_INTERACTIVE" != "0" ]; then
            COMMANDS="$COMMANDS; exec /bin/bash"
        fi
        run_container "$TOOLCHAIN_TARGET" /bin/bash -c "$COMMANDS"
        ;;
    "")
        # Just print our own header comment as usage/help
        echo "ERROR: Please specify a command." >&2
        echo ""
        echo "docker-ci.sh:"
        NUM_COMMENT_LINES=$(awk '/^#/{count++; next} {exit} END{print count}' "$0")
        TAIL_LINES=$((NUM_COMMENT_LINES-1))
        head -n "$NUM_COMMENT_LINES" "$0" | tail -n "$TAIL_LINES" | sed 's/^#//g'
        ;;
    *)
        echo "ERROR: Unknown command '$COMMAND'" >&2
        exit 1
        ;;
esac
