# TEMPORARY: samples memory use inside the CI build container, for diagnosing MSBuild
# nodes that exit prematurely in test-windows-crashpad. Runs until the container exits.
# Writes detailed samples to $OutFile, and a one-line summary of each sample to stdout so
# that it appears in the job log even if the job times out before uploading artifacts.
param([string]$OutFile = "C:\mnt\msbuild-diag\memory.log")

function To-Mb($bytes) { [math]::Round($bytes / 1MB) }

"processors: $([Environment]::ProcessorCount)" | Out-File -FilePath $OutFile -Encoding utf8
while ($true) {
    $os = Get-CimInstance Win32_OperatingSystem
    $procs = Get-Process
    $cl = @($procs | Where-Object { $_.ProcessName -eq "cl" })
    $msbuild = @($procs | Where-Object { $_.ProcessName -eq "MSBuild" })
    $cl_max = ($cl | Measure-Object PrivateMemorySize64 -Maximum).Maximum
    $summary = "MEMSAMPLE $(Get-Date -Format HH:mm:ss) " +
        "total_private_mb=$(To-Mb ($procs | Measure-Object PrivateMemorySize64 -Sum).Sum) " +
        "cl_count=$($cl.Count) cl_private_mb=$(To-Mb ($cl | Measure-Object PrivateMemorySize64 -Sum).Sum) " +
        "cl_max_mb=$(To-Mb $cl_max) " +
        "msbuild_count=$($msbuild.Count) msbuild_private_mb=$(To-Mb ($msbuild | Measure-Object PrivateMemorySize64 -Sum).Sum) " +
        "os_free_mb=$(To-Mb ($os.FreePhysicalMemory * 1KB))"
    Write-Output $summary
    $lines = @($summary)
    $lines += $procs | Sort-Object PrivateMemorySize64 -Descending | Select-Object -First 12 |
        ForEach-Object { "  {0,-24} pid={1,-6} private_mb={2}" -f $_.ProcessName, $_.Id, (To-Mb $_.PrivateMemorySize64) }
    $lines | Out-File -FilePath $OutFile -Append -Encoding utf8
    Start-Sleep -Seconds 2
}
