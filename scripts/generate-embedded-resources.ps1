param(
    [string]$SourceRoot,
    [string]$OutputPath,
    [switch]$Check
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = Join-Path $repositoryRoot "app"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $SourceRoot "embedded_resources.qrc"
}

$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar)
$outputDirectory = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    throw "Embedded resource output directory does not exist: $outputDirectory"
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)

$requiredConfigFiles = @(
    "config\actions.json",
    "config\device-catalog.json",
    "config\workflows.json"
)
$resourceFiles = New-Object System.Collections.Generic.List[IO.FileInfo]
foreach ($relativePath in $requiredConfigFiles) {
    $path = Join-Path $SourceRoot $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required embedded configuration file is missing: $path"
    }
    $resourceFiles.Add((Get-Item -LiteralPath $path))
}

$flashRoot = Join-Path $SourceRoot "flash"
$firmwareFiles = @(Get-ChildItem -LiteralPath $flashRoot -Recurse -File | Where-Object {
    $_.Extension.ToLowerInvariant() -in @(".hex", ".bin")
})
if ($firmwareFiles.Count -eq 0) {
    throw "No firmware files were found for embedding in $flashRoot"
}
foreach ($file in $firmwareFiles) {
    $resourceFiles.Add($file)
}

$relativePaths = @($resourceFiles | ForEach-Object {
    if (-not $_.FullName.StartsWith(
        $SourceRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to embed a file outside the application source directory: $($_.FullName)"
    }
    $_.FullName.Substring($SourceRoot.Length + 1).Replace('\', '/')
} | Sort-Object)

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('<!DOCTYPE RCC>')
$lines.Add('<RCC version="1.0">')
$lines.Add('  <qresource prefix="/">')
foreach ($relativePath in $relativePaths) {
    $escapedPath = [Security.SecurityElement]::Escape($relativePath)
    $lines.Add("    <file>$escapedPath</file>")
}
$lines.Add('  </qresource>')
$lines.Add('</RCC>')
$content = ($lines -join "`r`n") + "`r`n"

$currentContent = if (Test-Path -LiteralPath $OutputPath -PathType Leaf) {
    [IO.File]::ReadAllText($OutputPath)
} else {
    $null
}
if ($currentContent -ceq $content) {
    Write-Host "Embedded resource manifest is up to date ($($firmwareFiles.Count) firmware file(s))."
    return
}
if ($Check) {
    throw "Embedded resource manifest is out of date. Run scripts\generate-embedded-resources.ps1."
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($OutputPath, $content, $utf8NoBom)
Write-Host "Generated embedded resource manifest '$OutputPath' with $($firmwareFiles.Count) firmware file(s)."
