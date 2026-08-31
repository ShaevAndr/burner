param(
    [ValidateSet("all", "internal", "external")]
    [string]$Edition = "all",
    [ValidatePattern("^\d+\.\d+\.\d+(\.\d+)?$")]
    [string]$Version = "1.0.0",
    [switch]$BuildApplication
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$packagingRoot = Join-Path $repositoryRoot "packaging\windows"
$installersRoot = Join-Path $repositoryRoot "installers"
$releasesRoot = Join-Path $repositoryRoot "releases"
$editions = if ($Edition -eq "all") { @("internal", "external") } else { @($Edition) }

if ($BuildApplication) {
    $buildScript = Join-Path $PSScriptRoot "build-releases.ps1"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $buildScript -Edition $Edition
    if ($LASTEXITCODE -ne 0) {
        throw "Application build failed."
    }
}

$isccCandidates = @(
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1),
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
$iscc = $isccCandidates | Select-Object -First 1
if (-not $iscc) {
    throw "Inno Setup 6 was not found. Install JRSoftware.InnoSetup with winget and run this script again."
}

New-Item -ItemType Directory -Path $installersRoot -Force | Out-Null

foreach ($currentEdition in $editions) {
    $releaseDirectory = Join-Path $releasesRoot $currentEdition
    $executable = Join-Path $releaseDirectory "device-workbench-$currentEdition.exe"
    $configDirectory = Join-Path $releaseDirectory "config"
    $flashDirectory = Join-Path $releaseDirectory "flash"
    $issFile = Join-Path $packagingRoot "$currentEdition.iss"

    if (-not (Test-Path -LiteralPath $executable)) {
        throw "Missing release executable: $executable"
    }
    foreach ($configName in @("actions.json", "device-catalog.json", "workflows.json")) {
        $configFile = Join-Path $configDirectory $configName
        if (-not (Test-Path -LiteralPath $configFile)) {
            throw "Missing installer configuration file: $configFile"
        }
    }
    $firmwareFiles = Get-ChildItem -LiteralPath $flashDirectory -Recurse -File |
        Where-Object { $_.Extension -in @(".bin", ".hex") }
    if (-not $firmwareFiles) {
        throw "No firmware files were found in $flashDirectory"
    }

    $portableBaseName = if ($currentEdition -eq "internal") {
        "obnovi-BOC-Internal-Portable-$Version"
    } else {
        "obnovi-BOC-External-Portable-$Version"
    }
    $portableArchive = Join-Path $releasesRoot "$portableBaseName.zip"
    if (Test-Path -LiteralPath $portableArchive) {
        Remove-Item -LiteralPath $portableArchive -Force
    }
    $tarCommand = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($tarCommand) {
        & $tarCommand.Source -a -c -f $portableArchive -C $releaseDirectory .
        if ($LASTEXITCODE -ne 0) {
            throw "Portable archive creation failed for the $currentEdition edition."
        }
    } else {
        Compress-Archive -Path (Join-Path $releaseDirectory "*") `
            -DestinationPath $portableArchive -CompressionLevel Optimal
    }
    Write-Host "Created portable release: $portableArchive"

    Write-Host "Packaging $currentEdition edition with $($firmwareFiles.Count) firmware file(s)..."
    & $iscc "/DMyAppVersion=$Version" $issFile
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup failed for the $currentEdition edition."
    }
}

Get-ChildItem -LiteralPath $installersRoot -Filter "*-Setup-$Version.exe" -File |
    Select-Object FullName, Length, LastWriteTime
Get-ChildItem -LiteralPath $releasesRoot -Filter "*-$Version.zip" -File |
    Select-Object FullName, Length, LastWriteTime
