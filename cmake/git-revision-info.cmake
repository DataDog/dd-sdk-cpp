# This script is included when DD_ENABLE_GIT_REVISION_INFO is enabled: it assumes that
# the SDK is being built from a git repository and that git is installed. On success, it
# will populate the following values:
#
# - DD_GIT_BRANCH_NAME: Name of the git branch checked out at configure-time.
#   $(git rev-parse --abbrev-ref HEAD)
#
# - DD_GIT_COMMIT_SHA: Hash of the commit checked out at configure-time.
#   $(git rev-parse HEAD)
#
# - DD_GIT_HAS_PENDING_SRC_CHANGES: 'TRUE' if src/ was not clean at configure-time.
#   $([[ -n $(git status --porcelain src/) ]] && echo 'TRUE')
#
# - DD_SRC_HASH: Hash of all files in src/, including any pending local changes.
#   $(git ls-files -s src | git hash-object --stdin)
#
# - DD_GIT_REVISION_ID: Shorthand identifier that combines these details:
#   - If no pending changes: {branch}-{commit-sha}
#   - If src/ is dirty:      {branch}-{commit-sha}-{src-hash}
#   Where 'branch' is condensed (e.g. 'jdoe/RUM-1234/add-stuff' becomes 'rum1234') and
#   concatenated hashes are shortened to the first 7 characters.
#
# Note that these values are computed only at CMake configuration time; they are not
# recomputed for each build.

# Store the current git branch name in DD_GIT_BRANCH_NAME
execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${DD_SDK_ROOT_DIR}
    RESULT_VARIABLE DD_GIT_REV_PARSE_RESULT
    OUTPUT_VARIABLE DD_GIT_BRANCH_NAME
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT DD_GIT_REV_PARSE_RESULT EQUAL 0 OR NOT DD_GIT_BRANCH_NAME)
    message(FATAL_ERROR "Unable to stamp build with git revision info: root directory is not a git repo! Reconfigure with DD_ENABLE_GIT_REVISION_INFO=OFF")
    return()
endif()
message(STATUS "git branch: ${DD_GIT_BRANCH_NAME}")

# Store the commit hash at the latest revision in DD_GIT_COMMIT_SHA
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${DD_SDK_ROOT_DIR}
    RESULT_VARIABLE DD_GIT_REV_PARSE_RESULT
    OUTPUT_VARIABLE DD_GIT_COMMIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT DD_GIT_REV_PARSE_RESULT EQUAL 0 OR NOT DD_GIT_COMMIT_SHA)
    message(FATAL_ERROR "Unable to stamp build with git revision info: git rev-parse failed! Reconfigure with DD_ENABLE_GIT_REVISION_INFO=OFF")
    return()
endif()
message(STATUS "git commit sha: ${DD_GIT_COMMIT_SHA}")

# Run git status --porcelain src/ to see if there are any pending changes (staged or
# unstaged) that are significant to the SDK library
execute_process(
    COMMAND git status --porcelain src/
    WORKING_DIRECTORY ${DD_SDK_ROOT_DIR}
    RESULT_VARIABLE DD_GIT_STATUS_RESULT
    OUTPUT_VARIABLE DD_GIT_STATUS_PORCELAIN_SRC_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT DD_GIT_STATUS_RESULT EQUAL 0)
    message(FATAL_ERROR "Unable to stamp build with git revision info: git status failed! Reconfigure with DD_ENABLE_GIT_REVISION_INFO=OFF")
    return()
endif()
if(DD_GIT_STATUS_PORCELAIN_SRC_OUTPUT)
    set(DD_GIT_HAS_PENDING_SRC_CHANGES TRUE)
    message(STATUS "src is dirty: pending git changes detected")
else()
    message(STATUS "src is clean: no pending git changes")
endif()

# Prepare a function that effectively runs:
# - git ls-files -s src | git hash-object --stdin
# ...and stores the result in the given DD_SRC_HASH
function(compute_src_hash GIT_INDEX_FILE)
    # Get the list of tracked files in src/ with their mode, hash, and path
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env GIT_INDEX_FILE=${GIT_INDEX_FILE} git ls-files -s src
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE GIT_LS_FILES_RESULT
        OUTPUT_VARIABLE GIT_LS_FILES_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT GIT_LS_FILES_RESULT EQUAL 0 OR NOT GIT_LS_FILES_OUTPUT)
        message(FATAL_ERROR "Unable to stamp build with git revision info: git ls-files failed! Reconfigure with DD_ENABLE_GIT_REVISION_INFO=OFF")
    endif()

    # Hash that output using git hash-object
    file(WRITE "${DD_SDK_ROOT_DIR}/tmp-ls-files-src" "${GIT_LS_FILES_OUTPUT}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env GIT_INDEX_FILE=${GIT_INDEX_FILE} git hash-object --stdin
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        INPUT_FILE "${DD_SDK_ROOT_DIR}/tmp-ls-files-src"
        RESULT_VARIABLE GIT_HASH_OBJECT_RESULT
        OUTPUT_VARIABLE DD_SRC_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    file(REMOVE "${DD_SDK_ROOT_DIR}/tmp-ls-files-src")

    if(NOT GIT_HASH_OBJECT_RESULT EQUAL 0 OR NOT DD_SRC_HASH)
        message(FATAL_ERROR "Unable to stamp build with git revision info: git hash-object failed! Reconfigure with DD_ENABLE_GIT_REVISION_INFO=OFF")
    endif()
    set(DD_SRC_HASH "${DD_SRC_HASH}" PARENT_SCOPE)
endfunction()

# If we have pending changes in src/, create a temporary copy of the git index, add
# those changes to it, then compute an accurate hash of src/ from that index
if (DD_GIT_HAS_PENDING_SRC_CHANGES)
    # Copy .git/index to tmp-git-index
    file(COPY ${DD_SDK_ROOT_DIR}/.git/index DESTINATION ${DD_SDK_ROOT_DIR})
    file(RENAME ${DD_SDK_ROOT_DIR}/index ${DD_SDK_ROOT_DIR}/tmp-git-index)

    # Run git add -u src/ to stage all modified and untracked files in our temp index
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env GIT_INDEX_FILE=tmp-git-index git add -u src/
        WORKING_DIRECTORY ${DD_SDK_ROOT_DIR}
        RESULT_VARIABLE DD_GIT_ADD_RESULT
        ERROR_QUIET
    )
    if(NOT DD_GIT_ADD_RESULT EQUAL 0)
        message(FATAL_ERROR "Unable to stamp build with git revision info: git add failed! Reconfigure with DD_ENABLE_GIT_REVISION_INFO=OFF")
    endif()

    # Compute the hash of src using our temp index, so that it encompasses our untracked
    # changes
    compute_src_hash(tmp-git-index)

    # Delete tmp-git-index
    file(REMOVE ${DD_SDK_ROOT_DIR}/tmp-git-index)
else()
    # With no pending changes to src/, we can just compute the hash as-is
    compute_src_hash(.git/index)
endif()
message(STATUS "git hash of src: ${DD_SRC_HASH}")

# Compute a revision identifier

# Split on '/' by substituting the CMake list item delimiter ';'
string(REPLACE "/" ";" DD_GIT_BRANCH_TOKENS "${DD_GIT_BRANCH_NAME}")
list(LENGTH DD_GIT_BRANCH_TOKENS DD_GIT_BRANCH_NUM_TOKENS)

# Take the second '/'-delimited token if present; otherwise take the first, remove all
# hyphens, convert to lowercase, and cap the length at 16 characters
if(DD_GIT_BRANCH_NUM_TOKENS GREATER 1)
    list(GET DD_GIT_BRANCH_TOKENS 1 DD_GIT_BRANCH_TOKEN)
else()
    list(GET DD_GIT_BRANCH_TOKENS 0 DD_GIT_BRANCH_TOKEN)
endif()
string(TOLOWER "${DD_GIT_BRANCH_TOKEN}" DD_GIT_BRANCH_TOKEN)
string(REPLACE "-" "" DD_GIT_BRANCH_TOKEN "${DD_GIT_BRANCH_TOKEN}")
string(SUBSTRING "${DD_GIT_BRANCH_TOKEN}" 0 16 DD_GIT_BRANCH_TOKEN)

# Create DD_GIT_REVISION_ID by concatenating this short branch name with the first 7
# characters of the commit SHA
string(SUBSTRING "${DD_GIT_COMMIT_SHA}" 0 7 DD_GIT_COMMIT_SHORT_SHA)
set(DD_GIT_REVISION_ID "${DD_GIT_BRANCH_TOKEN}-${DD_GIT_COMMIT_SHORT_SHA}")

# If the src directory is dirty, append another short SHA describing the actual state of
# the files within src/
if(DD_GIT_HAS_PENDING_SRC_CHANGES)
    string(SUBSTRING "${DD_SRC_HASH}" 0 7 DD_SRC_SHORT_HASH)
    set(DD_GIT_REVISION_ID "${DD_GIT_REVISION_ID}-${DD_SRC_SHORT_HASH}")
endif()
message(STATUS "revision ID: ${DD_GIT_REVISION_ID}")
