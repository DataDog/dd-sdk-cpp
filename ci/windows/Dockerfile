ARG BASE_IMAGE=mcr.microsoft.com/dotnet/framework/runtime:4.8-windowsservercore-ltsc2022
FROM ${BASE_IMAGE}

ENV WINDOWS_VERSION=2022
ENV TARGET_ARCH=x64

# Image is built with ./ci/windows as the build context: this is the default source
# directory for `RUN --mount`, so working from `/mnt` puts us in the directory
# containing the PowerShell scripts we use to install build dependencies
WORKDIR /mnt

SHELL ["powershell", "-Command"]
RUN --mount=type=bind,dst=/mnt .\build-toolchain-image.ps1

WORKDIR c:/
