param(
    [ValidateSet("all", "internal", "external")]
    [string]$Edition = "all",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $repositoryRoot "app"
$buildRoot = Join-Path $repositoryRoot "build"
$releasesRoot = Join-Path $repositoryRoot "releases"

$qmakeCommand = Get-Command qmake.exe -ErrorAction SilentlyContinue
$qmake = if ($qmakeCommand) {
    $qmakeCommand.Source
} else {
    "C:\Qt\Qt5.12.12\5.12.12\mingw73_64\bin\qmake.exe"
}
if (-not (Test-Path -LiteralPath $qmake)) {
    throw "qmake.exe was not found. Add Qt 5.12 to PATH or update the fallback path in this script."
}

$qtBin = Split-Path -Parent $qmake
$deployTool = Join-Path $qtBin "windeployqt.exe"
$makeCandidates = @(
    (Join-Path $qtBin "mingw32-make.exe"),
    "C:\Qt\Qt5.12.12\Tools\mingw730_64\bin\mingw32-make.exe"
)
$make = $makeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $make) {
    throw "mingw32-make.exe was not found."
}

$editions = if ($Edition -eq "all") { @("internal", "external") } else { @($Edition) }

foreach ($currentEdition in $editions) {
    $buildDirectory = Join-Path $buildRoot $currentEdition
    $releaseDirectory = Join-Path $releasesRoot $currentEdition
    $projectFile = Join-Path $sourceRoot "device-workbench-$currentEdition.pro"
    $executable = Join-Path $releaseDirectory "device-workbench-$currentEdition.exe"

    if ($Clean -and (Test-Path -LiteralPath $buildDirectory)) {
        $resolved = (Resolve-Path -LiteralPath $buildDirectory).Path
        $expectedRoot = (Resolve-Path -LiteralPath $repositoryRoot).Path
        if (-not $resolved.StartsWith($expectedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove a directory outside the repository: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }

    New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null

    Push-Location $buildDirectory
    try {
        & $qmake $projectFile "CONFIG+=release"
        if ($LASTEXITCODE -ne 0) { throw "qmake failed for $currentEdition" }
        & $make -j4
        if ($LASTEXITCODE -ne 0) { throw "Build failed for $currentEdition" }
    } finally {
        Pop-Location
    }

    if (-not (Test-Path -LiteralPath $executable)) {
        throw "Build completed without the expected executable: $executable"
    }
    if (Test-Path -LiteralPath $deployTool) {
        & $deployTool --release --no-translations $executable
        if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for $currentEdition" }
    }

    Copy-Item -LiteralPath (Join-Path $sourceRoot "config") -Destination $releaseDirectory -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $sourceRoot "flash") -Destination $releaseDirectory -Recurse -Force
    Write-Host "Built $currentEdition edition: $executable"
}
