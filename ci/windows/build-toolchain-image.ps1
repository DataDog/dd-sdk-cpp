$ErrorActionPreference = 'Stop'

. .\helpers.ps1

try {
    .\install-python.ps1 -Version "3.12.6" -Sha256 "5914748e6580e70bedeb7c537a0832b3071de9e09a2e4e7e3d28060616045e0a"
    .\install-cmake.ps1 -Version "3.30.2" -Sha256 "31f799a9e7756305f74cd821970a793e599ead230925392886f45aed897a3c0e"
    .\install-vstudio.ps1 -Version "17.14" -Url "https://download.visualstudio.microsoft.com/download/pr/e98d75fa-91b1-47a1-9cb7-b6556de592c5/b4fab6d6d479c38a6081ec95e32d7a105d21d19b6ae7f97371f716dbea08303d/vs_BuildTools.exe" -Sha256 "B4FAB6D6D479C38A6081EC95E32D7A105D21D19B6AE7F97371F716DBEA08303D"
} catch {
    Write-Host -ForegroundColor Red "Error installing build dependencies"
    Write-Host -ForegroundColor Red $($_.ScriptStackTrace)
    exit -1
} finally {
    Remove-Item -Recurse -Force c:\tmp\* -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $Env:TEMP\* -ErrorAction SilentlyContinue
}
