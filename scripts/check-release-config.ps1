$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$configRoot = Join-Path $repositoryRoot "app\config"
$sourceRoot = Join-Path $repositoryRoot "app"
$flashRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot "flash"))

# Check source files before qmake embeds them. The executable's --check-config
# remains the authoritative edition-specific validation. Do not call the catalog
# synchronizer here: its -Check mode compares generated JSON text and rejects
# harmless changes to whitespace or property order.

function Read-ConfigJson {
    param([Parameter(Mandatory = $true)][string]$Name)
    $path = Join-Path $configRoot $Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required configuration file is missing: $path"
    }
    return (Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop)
}

function Assert-UniqueIds {
    param(
        [Parameter(Mandatory = $true)][object[]]$Items,
        [Parameter(Mandatory = $true)][string]$Kind,
        [string]$IdProperty = "id"
    )
    $seen = @{}
    foreach ($item in $Items) {
        $id = [string]$item.$IdProperty
        if ([string]::IsNullOrWhiteSpace($id)) {
            throw "$Kind has an empty ID"
        }
        if ($seen.ContainsKey($id)) {
            throw "$Kind has a duplicate ID: $id"
        }
        $seen[$id] = $true
    }
    return $seen
}

function Assert-ArtifactSource {
    param([Parameter(Mandatory = $true)]$Artifact)
    $relativePath = [string]$Artifact.relativePath
    if ([string]::IsNullOrWhiteSpace($relativePath)) {
        return # A detection-only firmware version has no file.
    }
    if (-not $relativePath.StartsWith("flash/", [StringComparison]::Ordinal)) {
        throw "Firmware artifact path must start with flash/: $relativePath"
    }
    $path = [IO.Path]::GetFullPath((Join-Path $sourceRoot ($relativePath -replace '/', [IO.Path]::DirectorySeparatorChar)))
    if (-not $path.StartsWith($flashRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Firmware artifact is missing or outside app/flash: $relativePath"
    }
    $expectedHash = [string]$Artifact.sha256
    if ($expectedHash -notmatch '^[0-9A-Fa-f]{64}$' -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $expectedHash) {
        throw "Firmware artifact SHA-256 differs from catalog: $relativePath"
    }
}

$catalog = Read-ConfigJson "device-catalog.json"
$actions = Read-ConfigJson "actions.json"
$workflows = Read-ConfigJson "workflows.json"
if ($catalog.schemaVersion -ne 4 -or $actions.schemaVersion -ne 1 -or
    $workflows.schemaVersion -ne 1) {
    throw "Unsupported configuration schema version"
}
if (-not $catalog.devices -or -not $actions.actions -or -not $workflows.workflows) {
    throw "Configuration must contain devices, actions and workflows"
}

$null = Assert-UniqueIds -Items @($catalog.devices) -Kind "Device profile"
$null = Assert-UniqueIds -Items @($catalog.firmwareCatalogs) -Kind "Firmware catalog" -IdProperty "deviceId"
$null = Assert-UniqueIds -Items @($actions.actions) -Kind "Action"
$workflowIds = Assert-UniqueIds -Items @($workflows.workflows) -Kind "Workflow"
foreach ($action in @($actions.actions)) {
    $workflowId = [string]$action.workflow
    if (-not $workflowIds.ContainsKey($workflowId)) {
        throw "Action '$($action.id)' refers to unknown workflow '$workflowId'"
    }
}
foreach ($firmwareCatalog in @($catalog.firmwareCatalogs)) {
    foreach ($version in @($firmwareCatalog.versions)) {
        if ($null -ne $version.artifact) {
            Assert-ArtifactSource -Artifact $version.artifact
        }
    }
    foreach ($artifact in @($firmwareCatalog.artifacts)) {
        if ($null -ne $artifact) {
            Assert-ArtifactSource -Artifact $artifact
        }
    }
}
Write-Host "Source configuration preflight passed."
