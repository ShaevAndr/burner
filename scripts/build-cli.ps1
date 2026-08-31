param(
    [switch]$Clean,
    [ValidatePattern("^\d+\.\d+\.\d+(\.\d+)?$")]
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $repositoryRoot "tools\obnovi-boc-cli\obnovi-boc-cli.pro"
$buildDirectory = Join-Path $repositoryRoot "build\cli"
$outputDirectory = Join-Path $repositoryRoot "releases\tools"
$executable = Join-Path $outputDirectory "obnovi-boc-cli.exe"
$readmeSource = Join-Path $repositoryRoot "tools\obnovi-boc-cli\README.md"
$readmeTarget = Join-Path $outputDirectory "README.md"
$archive = Join-Path $repositoryRoot "releases\obnovi-BOC-CLI-$Version.zip"

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
$makeCandidates = @(
    (Join-Path $qtBin "mingw32-make.exe"),
    "C:\Qt\Qt5.12.12\Tools\mingw730_64\bin\mingw32-make.exe"
)
$make = $makeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $make) {
    throw "mingw32-make.exe was not found."
}

if ($Clean -and (Test-Path -LiteralPath $buildDirectory)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $buildDirectory).Path
    $resolvedRoot = (Resolve-Path -LiteralPath $repositoryRoot).Path
    if (-not $resolvedBuild.StartsWith(
            $resolvedRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a directory outside the repository: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
Push-Location $buildDirectory
try {
    & $qmake $projectFile "CONFIG+=release"
    if ($LASTEXITCODE -ne 0) {
        throw "qmake failed for obnovi-boc-cli"
    }
    & $make -j4
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for obnovi-boc-cli"
    }
} finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Build completed without the expected executable: $executable"
}

Copy-Item -LiteralPath $readmeSource -Destination $readmeTarget -Force
if (Test-Path -LiteralPath $archive) {
    $resolvedArchive = (Resolve-Path -LiteralPath $archive).Path
    $resolvedReleases = (Resolve-Path -LiteralPath (Join-Path $repositoryRoot "releases")).Path
    if (-not $resolvedArchive.StartsWith(
            $resolvedReleases + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace an archive outside the releases directory: $resolvedArchive"
    }
    Remove-Item -LiteralPath $resolvedArchive -Force
}
$tarCommand = Get-Command tar.exe -ErrorAction SilentlyContinue
if ($tarCommand) {
    & $tarCommand.Source -a -c -f $archive -C $outputDirectory .
    if ($LASTEXITCODE -ne 0) {
        throw "CLI archive creation failed."
    }
} else {
    Compress-Archive -Path (Join-Path $outputDirectory "*") `
        -DestinationPath $archive -CompressionLevel Optimal
}

$file = Get-Item -LiteralPath $executable
$hash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
Write-Host "Built CLI: $($file.FullName)"
Write-Host "Size: $($file.Length) bytes"
Write-Host "SHA-256: $hash"
Write-Host "Created CLI archive: $archive"
