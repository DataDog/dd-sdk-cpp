param (
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][string]$Sha256
)

$ErrorActionPreference = 'Stop'

$msiUrl = "https://github.com/Kitware/CMake/releases/download/v$($Version)/cmake-$($Version)-windows-x86_64.msi"
$msiPath = Join-Path ([IO.Path]::GetTempPath()) 'cmake.msi'

Write-Host "Downloading CMake installer from: {0}" -f $msiUrl
Get-RemoteFile -RemoteFile $msiUrl -LocalFile $msiPath -VerifyHash $Sha256

Write-Host "Installing CMake {0}..." -f $Version
Start-Process msiexec -ArgumentList "/q /i $($msiPath) ADD_CMAKE_TO_PATH=System" -Wait

Remove-Item $msiPath
Reload-Path
Write-Host -ForegroundColor Green "CMake {0} installed." -f $Version
