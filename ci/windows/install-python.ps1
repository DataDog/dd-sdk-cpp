param (
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][string]$Sha256
)

$ErrorActionPreference = 'Stop'

$exeUrl = "https://www.python.org/ftp/python/$($Version)/python-$($Version)-amd64.exe"
$exePath = Join-Path ([IO.Path]::GetTempPath()) 'python.exe'

$shortVer = ($Version -split '\.')[0..1] -join ''  # e.g. '312' from '3.12.6'

Write-Host "Downloading Python installer from: $exeUrl"
Get-RemoteFile -RemoteFile $exeUrl -LocalFile $exePath -VerifyHash $Sha256

Write-Host "Installing Python $Version..."
Start-Process $exePath -ArgumentList '/quiet InstallAllUsers=1' -Wait
Add-EnvironmentVariable -Variable PYTHONUTF8 -Value 1 -Local -Global
Add-ToPath "C:\Program Files\Python$shortVer;C:\Program Files\Python$shortVer\Scripts" -Global -Local

Remove-Item $exePath
Reload-Path
Write-Host -ForegroundColor Green "Python $Version installed."
