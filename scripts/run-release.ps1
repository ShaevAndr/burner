param(
    [ValidateSet("internal", "external")]
    [string]$Edition = "internal",
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$releaseDirectory = Join-Path $repositoryRoot "releases\$Edition"
$executable = Join-Path $releaseDirectory "device-workbench-$Edition.exe"

if ($Build) {
    $buildScript = Join-Path $PSScriptRoot "build-releases.ps1"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript -Edition $Edition
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for the $Edition edition."
    }
}

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Release executable was not found: $executable. Run build-releases.ps1 first."
}

Start-Process -FilePath $executable -WorkingDirectory $releaseDirectory
