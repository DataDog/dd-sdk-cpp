param (
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][string]$Sha256
)

$ErrorActionPreference = 'Stop'

$zipUrl = "https://releases.hashicorp.com/vault/$Version/vault_${Version}_windows_amd64.zip"
$zipPath = Join-Path ([IO.Path]::GetTempPath()) 'vault.zip'

Write-Host "Downloading vault archive from: $zipUrl"
Get-RemoteFile -RemoteFile $zipUrl -LocalFile $zipPath -VerifyHash $Sha256

if(! (test-path "c:\devtools\vault")){
    md c:\devtools\vault
}
& '7z' x -oc:\devtools\vault $zipPath
Remove-Item $zipPath

Add-ToPath -NewPath "c:\devtools\vault" -Local -Global

Write-Host -ForegroundColor Green "vault $Version installed."
