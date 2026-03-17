# Load environment variables from a .env file.
# Usage:
#   . .\load-env.ps1            # loads .env from current directory
#   . .\load-env.ps1 -Path C:\path\to\.env

param(
    [string]$Path = (Join-Path $PSScriptRoot ".env")
)

if (-not (Test-Path $Path)) {
    Write-Error "File not found: $Path"
    return
}

$count = 0
Get-Content $Path | ForEach-Object {
    $line = $_.Trim()
    # Skip empty lines and comments
    if ($line -eq "" -or $line.StartsWith("#")) { return }

    $eq = $line.IndexOf("=")
    if ($eq -lt 1) { return }

    $name = $line.Substring(0, $eq).Trim()
    $value = $line.Substring($eq + 1).Trim()
    # Strip surrounding quotes if present
    if (($value.StartsWith('"') -and $value.EndsWith('"')) -or
        ($value.StartsWith("'") -and $value.EndsWith("'"))) {
        $value = $value.Substring(1, $value.Length - 2)
    }

    [System.Environment]::SetEnvironmentVariable($name, $value, "Process")
    Write-Host "  $name = $($value.Substring(0, [Math]::Min(8, $value.Length)))..."
    $count++
}

Write-Host "Loaded $count variable(s) from $Path"
