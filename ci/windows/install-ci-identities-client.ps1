param (
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][string]$Sha256
)

$ErrorActionPreference = 'Stop'

# In Windows CI jobs, we need access to Vault in order to retrieve secret values for
# integration tests etc. - this requires that we use the ci-identities-gitlab-job-client
# tool in order to assume the IAM role associated with our job's CI Identity
# https://datadoghq.atlassian.net/wiki/spaces/SECENG/pages/5324145720/Using+CI+Identities
$exeUrl = "https://www.python.org/ftp/python/$($Version)/python-$($Version)-amd64.exe"

# Install to c:\devtools\ddbuild
if(! (test-path "c:\devtools\ddbuild")){
    md c:\devtools\ddbuild
}
$exePath = "c:\devtools\ddbuild\ci-identities-gitlab-job-client-windows-amd64.exe"
Get-RemoteFile -RemoteFile $exeUrl -LocalFile $exePath -VerifyHash $Sha256

Write-Host -ForegroundColor Green "ci-identities-gitlab-job-client-windows-amd64.exe $Version installed."
