# CMake toolchain file for building dd-sdk-cpp with Unreal Engine's Linux toolchain, so
# that the resulting static library links into Unreal Engine projects.
#
# Unreal links Linux modules against the LLVM libc++ bundled in its toolchain, rather
# than GNU libstdc++, and compiles against the toolchain's sysroot (glibc 2.28). This
# file builds dd-sdk-cpp against the same libc++ headers and sysroot, so the library
# uses the same libc++ ABI and hardening mode as Unreal, and references no glibc symbol
# versions newer than 2.28.
#
# The ue-clang20 stage in ci/linux/Dockerfile installs this file next to the
# toolchain's x86_64-unknown-linux-gnu/ directory and points CMAKE_TOOLCHAIN_FILE at it.

set(DD_UE_TOOLCHAIN_TARGET "x86_64-unknown-linux-gnu")
set(DD_UE_TOOLCHAIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/${DD_UE_TOOLCHAIN_TARGET}")
if(NOT EXISTS "${DD_UE_TOOLCHAIN_ROOT}/bin/clang++")
    message(FATAL_ERROR "Unreal toolchain not found at ${DD_UE_TOOLCHAIN_ROOT}")
endif()

# The toolchain directory doubles as the sysroot: it contains the glibc 2.28 headers and
# libraries under usr/ and lib64/, and libc++ under include/c++/v1 and lib64/
set(CMAKE_SYSROOT "${DD_UE_TOOLCHAIN_ROOT}")

set(CMAKE_C_COMPILER "${DD_UE_TOOLCHAIN_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "${DD_UE_TOOLCHAIN_ROOT}/bin/clang++")
set(CMAKE_C_COMPILER_TARGET "${DD_UE_TOOLCHAIN_TARGET}")
set(CMAKE_CXX_COMPILER_TARGET "${DD_UE_TOOLCHAIN_TARGET}")
set(CMAKE_AR "${DD_UE_TOOLCHAIN_ROOT}/bin/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${DD_UE_TOOLCHAIN_ROOT}/bin/llvm-ranlib" CACHE FILEPATH "")

# The toolchain has no llvm-nm or llvm-readelf, but includes the binutils equivalents.
# tools/check-package reads these paths from CMakeCache.txt to inspect the library.
set(CMAKE_NM "${DD_UE_TOOLCHAIN_ROOT}/bin/${DD_UE_TOOLCHAIN_TARGET}-nm" CACHE FILEPATH "")
set(CMAKE_READELF "${DD_UE_TOOLCHAIN_ROOT}/bin/${DD_UE_TOOLCHAIN_TARGET}-readelf"
    CACHE FILEPATH "")

# Compile against the toolchain's libc++ headers. The sysroot also contains GCC's
# libstdc++, so -stdlib=libc++ is what keeps libstdc++ out of the build.
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")

# Executables built alongside the library (such as the `version` tool that the package
# target runs) link libc++ and libc++abi statically, as Unreal does, so that they run
# without libc++ installed. -nostdlib++ keeps the driver from adding a C++ standard
# library of its own.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -nostdlib++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld -nostdlib++")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-fuse-ld=lld -nostdlib++")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT
    "${DD_UE_TOOLCHAIN_ROOT}/lib64/libc++.a ${DD_UE_TOOLCHAIN_ROOT}/lib64/libc++abi.a"
)
