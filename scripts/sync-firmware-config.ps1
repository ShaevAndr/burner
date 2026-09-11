param(
    [string]$FlashRoot,
    [string]$CatalogPath,
    [switch]$Check
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($FlashRoot)) {
    $FlashRoot = Join-Path $repositoryRoot "app\flash"
}
if ([string]::IsNullOrWhiteSpace($CatalogPath)) {
    $CatalogPath = Join-Path $repositoryRoot "app\config\device-catalog.json"
}

$FlashRoot = (Resolve-Path -LiteralPath $FlashRoot).Path
$CatalogPath = (Resolve-Path -LiteralPath $CatalogPath).Path
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$supportedExtensions = @(".hex", ".bin")
$monthPattern = "Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec"
$buildStampPattern = "(?<date>(?:$monthPattern)\s{1,2}\d{1,2}\s\d{4})[\x00-\x20]{0,32}(?<time>\d{2}:\d{2}:\d{2})"
$culture = [Globalization.CultureInfo]::GetCultureInfo("en-US")
$defaultFlashNum = 1
$bootloaderAddressBase = [uint64]0x08000000
$applicationAddressBase = [uint64]0x08040000

function Format-HexAddress {
    param([Parameter(Mandatory = $true)][uint64]$Address)
    return "0x" + $Address.ToString("X8", [Globalization.CultureInfo]::InvariantCulture)
}

function Read-IntelHexData {
    param([Parameter(Mandatory = $true)][string]$Path)

    $data = New-Object System.Collections.Generic.List[byte]
    $lineNumber = 0
    $hasEof = $false
    foreach ($rawLine in [IO.File]::ReadLines($Path, [Text.Encoding]::ASCII)) {
        ++$lineNumber
        $line = $rawLine.Trim()
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        if (-not $line.StartsWith(":")) {
            throw "Invalid Intel HEX line $lineNumber in '$Path': missing ':'"
        }

        $hex = $line.Substring(1)
        if (($hex.Length % 2) -ne 0 -or $hex.Length -lt 10 -or $hex -notmatch '^[0-9A-Fa-f]+$') {
            throw "Invalid Intel HEX line $lineNumber in '$Path'"
        }

        $record = New-Object byte[] ($hex.Length / 2)
        for ($index = 0; $index -lt $record.Length; ++$index) {
            $record[$index] = [Convert]::ToByte($hex.Substring($index * 2, 2), 16)
        }

        $byteCount = [int]$record[0]
        if ($record.Length -ne ($byteCount + 5)) {
            throw "Intel HEX byte count mismatch at line $lineNumber in '$Path'"
        }
        $checksum = 0
        foreach ($value in $record) {
            $checksum = ($checksum + $value) -band 0xFF
        }
        if ($checksum -ne 0) {
            throw "Intel HEX checksum mismatch at line $lineNumber in '$Path'"
        }

        $recordType = [int]$record[3]
        if ($recordType -eq 0) {
            for ($index = 0; $index -lt $byteCount; ++$index) {
                $data.Add($record[4 + $index])
            }
        } elseif ($recordType -eq 1) {
            $hasEof = $true
            break
        }
    }

    if (-not $hasEof) {
        throw "Intel HEX file '$Path' has no EOF record"
    }
    return $data.ToArray()
}

function Get-IntelHexAddressRange {
    param([Parameter(Mandatory = $true)][string]$Path)

    [uint64]$upperAddress = 0
    [uint64]$minimumAddress = [uint64]::MaxValue
    [uint64]$maximumAddress = 0
    $hasData = $false
    $lineNumber = 0
    foreach ($rawLine in [IO.File]::ReadLines($Path, [Text.Encoding]::ASCII)) {
        ++$lineNumber
        $line = $rawLine.Trim()
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $byteCount = [Convert]::ToInt32($line.Substring(1, 2), 16)
        $recordOffset = [Convert]::ToInt32($line.Substring(3, 4), 16)
        $recordType = [Convert]::ToInt32($line.Substring(7, 2), 16)
        if ($recordType -eq 0) {
            if ($byteCount -eq 0) {
                continue
            }
            [uint64]$recordStart = $upperAddress + [uint64]$recordOffset
            [uint64]$recordEnd = $recordStart + [uint64]$byteCount - 1
            if ($recordStart -lt $minimumAddress) {
                $minimumAddress = $recordStart
            }
            if ($recordEnd -gt $maximumAddress) {
                $maximumAddress = $recordEnd
            }
            $hasData = $true
        } elseif ($recordType -eq 2) {
            $segment = [Convert]::ToInt32($line.Substring(9, 4), 16)
            $upperAddress = [uint64]$segment * 16
        } elseif ($recordType -eq 4) {
            $linear = [Convert]::ToInt32($line.Substring(9, 4), 16)
            $upperAddress = [uint64]$linear * 65536
        }
    }

    if (-not $hasData) {
        throw "Intel HEX file '$Path' has no data records"
    }
    return [pscustomobject]@{
        Start = $minimumAddress
        End = $maximumAddress
    }
}

function Assert-FirmwareAddressBase {
    param(
        [Parameter(Mandatory = $true)][IO.FileInfo]$File,
        [Parameter(Mandatory = $true)][uint64]$ExpectedAddress,
        [Parameter(Mandatory = $true)][string]$Target
    )

    if ($File.Extension -ine ".hex") {
        return
    }
    $range = Get-IntelHexAddressRange -Path $File.FullName
    if ($range.Start -ne $ExpectedAddress) {
        throw "Firmware '$($File.FullName)' target '$Target' starts at $(Format-HexAddress $range.Start); expected $(Format-HexAddress $ExpectedAddress)"
    }
}

function Get-FirmwareBuildStamp {
    param([Parameter(Mandatory = $true)][IO.FileInfo]$File)

    $bytes = if ($File.Extension -ieq ".hex") {
        Read-IntelHexData -Path $File.FullName
    } else {
        [IO.File]::ReadAllBytes($File.FullName)
    }
    $text = [Text.Encoding]::ASCII.GetString($bytes)
    $matches = [regex]::Matches($text, $buildStampPattern)
    $stamps = @{}
    foreach ($match in $matches) {
        $dateText = $match.Groups["date"].Value
        $timeText = $match.Groups["time"].Value
        $normalized = (($dateText -replace '\s+', ' ') + " " + $timeText)
        $dateTime = [DateTime]::MinValue
        if (-not [DateTime]::TryParseExact(
                $normalized,
                "MMM d yyyy HH:mm:ss",
                $culture,
                [Globalization.DateTimeStyles]::None,
                [ref]$dateTime)) {
            continue
        }
        $key = $dateTime.ToString("yyyy-MM-dd HH:mm:ss", [Globalization.CultureInfo]::InvariantCulture)
        $stamps[$key] = [pscustomobject]@{
            DateTime = $dateTime
            DateText = $dateText
            TimeText = $timeText
        }
    }

    if ($stamps.Count -ne 1) {
        throw "Firmware '$($File.FullName)' must contain exactly one unique compiler date/time pair; found $($stamps.Count)"
    }
    return @($stamps.Values)[0]
}

function Copy-JsonObject {
    param([Parameter(Mandatory = $true)]$Value)
    return ($Value | ConvertTo-Json -Depth 100 -Compress | ConvertFrom-Json)
}

function Set-JsonProperty {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowNull()]$Value
    )
    if ($Object.PSObject.Properties[$Name]) {
        $Object.$Name = $Value
    } else {
        $Object | Add-Member -MemberType NoteProperty -Name $Name -Value $Value
    }
}

function ConvertTo-ProjectJson {
    param(
        [AllowNull()]$Value,
        [int]$Indent = 0,
        [string]$NewLine = "`n"
    )

    if ($null -eq $Value) {
        return "null"
    }
    if ($Value -is [string] -or $Value -is [char]) {
        return (ConvertTo-Json -InputObject ([string]$Value) -Compress)
    }
    if ($Value -is [bool]) {
        return $(if ($Value) { "true" } else { "false" })
    }
    if ($Value -is [byte] -or $Value -is [sbyte] -or
        $Value -is [int16] -or $Value -is [uint16] -or
        $Value -is [int32] -or $Value -is [uint32] -or
        $Value -is [int64] -or $Value -is [uint64] -or
        $Value -is [single] -or $Value -is [double] -or $Value -is [decimal]) {
        return ([Convert]::ToString($Value, [Globalization.CultureInfo]::InvariantCulture))
    }

    $padding = " " * $Indent
    $childPadding = " " * ($Indent + 2)
    if ($Value -is [Collections.IDictionary]) {
        $properties = @($Value.GetEnumerator() | ForEach-Object {
            [pscustomobject]@{ Name = [string]$_.Key; Value = $_.Value }
        })
    } elseif ($Value -is [Management.Automation.PSCustomObject]) {
        $properties = @($Value.PSObject.Properties | ForEach-Object {
            [pscustomobject]@{ Name = $_.Name; Value = $_.Value }
        })
    } else {
        $properties = $null
    }

    if ($null -ne $properties) {
        if ($properties.Count -eq 0) {
            return "{}"
        }
        $lines = foreach ($property in $properties) {
            $name = ConvertTo-ProjectJson -Value $property.Name -NewLine $NewLine
            $serialized = ConvertTo-ProjectJson -Value $property.Value -Indent ($Indent + 2) -NewLine $NewLine
            "$childPadding$name`: $serialized"
        }
        return "{$NewLine$($lines -join ",$NewLine")$NewLine$padding}"
    }

    if ($Value -is [Collections.IEnumerable]) {
        $items = @($Value)
        if ($items.Count -eq 0) {
            return "[]"
        }
        $lines = foreach ($item in $items) {
            $serialized = ConvertTo-ProjectJson -Value $item -Indent ($Indent + 2) -NewLine $NewLine
            "$childPadding$serialized"
        }
        return "[$NewLine$($lines -join ",$NewLine")$NewLine$padding]"
    }

    throw "Unsupported JSON value type: $($Value.GetType().FullName)"
}

function Get-RelativeFirmwareDirectory {
    param([Parameter(Mandatory = $true)]$Catalog)

    $directories = @($Catalog.versions | Where-Object {
        -not [string]::IsNullOrWhiteSpace([string]$_.artifact.relativePath)
    } | ForEach-Object {
        $path = [string]$_.artifact.relativePath
        if ($path -notmatch '^flash/(?<directory>.+)/[^/]+$') {
            throw "Firmware '$($_.id)' in catalog '$($Catalog.deviceId)' has an unsupported relativePath '$path'"
        }
        $Matches["directory"]
    } | Sort-Object -Unique)
    if ($directories.Count -ne 1) {
        throw "Catalog '$($Catalog.deviceId)' must use exactly one firmware directory; found $($directories.Count)"
    }
    return $directories[0]
}

$originalJson = [IO.File]::ReadAllText($CatalogPath, [Text.Encoding]::UTF8)
$catalogRoot = $originalJson | ConvertFrom-Json
if ($catalogRoot.schemaVersion -ne 4) {
    throw "Catalog '$CatalogPath' must use schemaVersion 4"
}

$allFirmwareFiles = @(Get-ChildItem -LiteralPath $FlashRoot -Recurse -File | Where-Object {
    $supportedExtensions -contains $_.Extension.ToLowerInvariant()
})
$assignedFiles = @{}
$changeMessages = New-Object System.Collections.Generic.List[string]

foreach ($catalog in @($catalogRoot.firmwareCatalogs)) {
    $existingVersions = @($catalog.versions)
    $installableVersions = @($existingVersions | Where-Object {
        -not [string]::IsNullOrWhiteSpace([string]$_.artifact.relativePath)
    })
    $detectionOnlyVersions = @($existingVersions | Where-Object {
        [string]::IsNullOrWhiteSpace([string]$_.artifact.relativePath)
    })
    if ($installableVersions.Count -eq 0) {
        throw "Catalog '$($catalog.deviceId)' has no template firmware entry"
    }

    $relativeDirectory = Get-RelativeFirmwareDirectory -Catalog $catalog
    $firmwareDirectory = Join-Path $FlashRoot ($relativeDirectory -replace '/', [IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $firmwareDirectory -PathType Container)) {
        throw "Firmware directory for '$($catalog.deviceId)' does not exist: $firmwareDirectory"
    }
    $firmwareFiles = @(Get-ChildItem -LiteralPath $firmwareDirectory -File | Where-Object {
        $supportedExtensions -contains $_.Extension.ToLowerInvariant()
    } | Sort-Object Name)
    if ($firmwareFiles.Count -eq 0) {
        throw "No .hex or .bin firmware files found for '$($catalog.deviceId)' in '$firmwareDirectory'"
    }

    $template = @($installableVersions | Where-Object { $_.artifact.default }) | Select-Object -First 1
    if (-not $template) {
        $template = $installableVersions[-1]
    }
    $existingById = @{}
    foreach ($version in $existingVersions) {
        $existingById[[string]$version.id] = $version
    }

    $scanned = New-Object System.Collections.Generic.List[object]
    $seenFirmwareIds = @{}
    foreach ($file in $firmwareFiles) {
        $assignedFiles[$file.FullName.ToLowerInvariant()] = $true
        $stamp = Get-FirmwareBuildStamp -File $file
        Assert-FirmwareAddressBase -File $file -ExpectedAddress $applicationAddressBase -Target "application"
        $firmwareId = "sw-" + $stamp.DateTime.ToString("yyyy-MM-dd-HH-mm-ss", [Globalization.CultureInfo]::InvariantCulture)
        if ($seenFirmwareIds.ContainsKey($firmwareId)) {
            throw "Files '$($seenFirmwareIds[$firmwareId])' and '$($file.FullName)' contain the same firmware version '$firmwareId'"
        }
        $seenFirmwareIds[$firmwareId] = $file.FullName

        $version = if ($existingById.ContainsKey($firmwareId)) {
            Copy-JsonObject -Value $existingById[$firmwareId]
        } else {
            $changeMessages.Add("ADD $($catalog.deviceId): $firmwareId ($($file.Name))")
            Copy-JsonObject -Value $template
        }

        $versionText = $stamp.DateTime.ToString("yyyy-MM-dd HH:mm:ss", [Globalization.CultureInfo]::InvariantCulture)
        $buildMonth = $stamp.DateTime.ToString("MMM", $culture)
        $buildDay = $stamp.DateTime.Day
        $buildDayPattern = if ($buildDay -lt 10) { "0?$buildDay" } else { [string]$buildDay }
        $buildYear = $stamp.DateTime.Year
        Set-JsonProperty -Object $version -Name "id" -Value $firmwareId
        Set-JsonProperty -Object $version -Name "version" -Value $versionText
        Set-JsonProperty -Object $version -Name "descriptionRegex" -Value "\(SW $buildMonth\s+$buildDayPattern $buildYear $($stamp.TimeText)\)$"
        Set-JsonProperty -Object $version.artifact -Name "target" -Value "application"
        Set-JsonProperty -Object $version.artifact -Name "relativePath" -Value "flash/$relativeDirectory/$($file.Name)"
        Set-JsonProperty -Object $version.artifact -Name "sha256" -Value (
            (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
        )
        Set-JsonProperty -Object $version.artifact -Name "format" -Value $(
            if ($file.Extension -ieq ".hex") { "intelHex" } else { "binary" }
        )
        Set-JsonProperty -Object $version.artifact -Name "addressBase" -Value (
            Format-HexAddress $applicationAddressBase)
        Set-JsonProperty -Object $version.artifact -Name "flashNum" -Value $defaultFlashNum
        if ($version.artifact.PSObject.Properties["offset"]) {
            $version.artifact.PSObject.Properties.Remove("offset")
        }
        $scanned.Add([pscustomobject]@{
            DateTime = $stamp.DateTime
            Version = $version
        })
    }

    foreach ($oldVersion in $installableVersions) {
        if (-not $seenFirmwareIds.ContainsKey([string]$oldVersion.id)) {
            $changeMessages.Add("REMOVE $($catalog.deviceId): $($oldVersion.id)")
        }
    }

    $scannedVersions = @($scanned | Sort-Object DateTime | ForEach-Object { $_.Version })
    $latestId = [string]$scannedVersions[-1].id
    foreach ($version in $scannedVersions) {
        if ([string]$version.id -eq $latestId) {
            Set-JsonProperty -Object $version.artifact -Name "default" -Value $true
        } elseif ($version.artifact.PSObject.Properties["default"]) {
            $version.artifact.PSObject.Properties.Remove("default")
        }
    }
    $catalog.versions = @($detectionOnlyVersions) + $scannedVersions

    $validFirmwareIds = @{}
    foreach ($version in @($catalog.versions)) {
        $validFirmwareIds[[string]$version.id] = $true
    }
    $transitionKeys = @{}
    $newTransitions = New-Object System.Collections.Generic.List[object]
    foreach ($transition in @($catalog.transitions)) {
        if (-not $validFirmwareIds.ContainsKey([string]$transition.from) -or
            -not $validFirmwareIds.ContainsKey([string]$transition.to)) {
            continue
        }
        $key = "$($transition.from)`n$($transition.to)"
        $transitionKeys[$key] = $true
        $newTransitions.Add((Copy-JsonObject -Value $transition))
    }
    $autoTransitions = -not $catalog.PSObject.Properties["autoTransitions"] -or
        [bool]$catalog.autoTransitions
    if ($autoTransitions) {
        foreach ($from in $scannedVersions) {
            foreach ($to in $scannedVersions) {
                $key = "$($from.id)`n$($to.id)"
                if (-not $transitionKeys.ContainsKey($key)) {
                    $transition = [pscustomobject][ordered]@{
                        from = [string]$from.id
                        to = [string]$to.id
                        enabled = $true
                    }
                    $newTransitions.Add($transition)
                    $transitionKeys[$key] = $true
                    $changeMessages.Add("ALLOW $($catalog.deviceId): $($from.id) -> $($to.id)")
                }
            }
        }
    }
    $catalog.transitions = @($newTransitions | ForEach-Object { $_ })

    # A bootloader is associated with a device by directory convention:
    # flash/<device-directory>/bootloader/*.hex|*.bin.  Unlike application
    # versions, it is a standalone artifact and may be written from any
    # recognized application version.
    $existingArtifacts = @($catalog.artifacts | Where-Object { $null -ne $_ })
    $existingBootloaders = @($existingArtifacts | Where-Object {
        [string]$_.target -eq "bootloader"
    })
    $otherArtifacts = @($existingArtifacts | Where-Object {
        [string]$_.target -ne "bootloader"
    })
    $existingBootloadersByVersion = @{}
    foreach ($artifact in $existingBootloaders) {
        if (-not [string]::IsNullOrWhiteSpace([string]$artifact.version)) {
            $existingBootloadersByVersion[[string]$artifact.version] = $artifact
        }
    }

    $bootloaderDirectory = Join-Path $firmwareDirectory "bootloader"
    $bootloaderFiles = if (Test-Path -LiteralPath $bootloaderDirectory -PathType Container) {
        @(Get-ChildItem -LiteralPath $bootloaderDirectory -File | Where-Object {
            $supportedExtensions -contains $_.Extension.ToLowerInvariant()
        } | Sort-Object Name)
    } else {
        @()
    }
    $scannedBootloaders = New-Object System.Collections.Generic.List[object]
    $seenBootloaderVersions = @{}
    foreach ($file in $bootloaderFiles) {
        $assignedFiles[$file.FullName.ToLowerInvariant()] = $true
        $stamp = Get-FirmwareBuildStamp -File $file
        Assert-FirmwareAddressBase -File $file -ExpectedAddress $bootloaderAddressBase -Target "bootloader"
        $versionText = $stamp.DateTime.ToString(
            "yyyy-MM-dd HH:mm:ss", [Globalization.CultureInfo]::InvariantCulture)
        if ($seenBootloaderVersions.ContainsKey($versionText)) {
            throw "Bootloader files '$($seenBootloaderVersions[$versionText])' and '$($file.FullName)' contain the same version '$versionText'"
        }
        $seenBootloaderVersions[$versionText] = $file.FullName

        $artifact = if ($existingBootloadersByVersion.ContainsKey($versionText)) {
            Copy-JsonObject -Value $existingBootloadersByVersion[$versionText]
        } else {
            $changeMessages.Add("ADD $($catalog.deviceId) bootloader: $versionText ($($file.Name))")
            [pscustomobject][ordered]@{
                target = "bootloader"
                title = "$($catalog.deviceId) Bootloader"
                version = $versionText
                relativePath = ""
                sha256 = ""
                format = ""
                addressBase = "0x08000000"
                flashNum = 1
                flashStrategy = "page-flash"
            }
        }

        Set-JsonProperty -Object $artifact -Name "target" -Value "bootloader"
        Set-JsonProperty -Object $artifact -Name "version" -Value $versionText
        Set-JsonProperty -Object $artifact -Name "relativePath" -Value (
            "flash/$relativeDirectory/bootloader/$($file.Name)")
        Set-JsonProperty -Object $artifact -Name "sha256" -Value (
            (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
        )
        Set-JsonProperty -Object $artifact -Name "format" -Value $(
            if ($file.Extension -ieq ".hex") { "intelHex" } else { "binary" }
        )
        Set-JsonProperty -Object $artifact -Name "addressBase" -Value (
            Format-HexAddress $bootloaderAddressBase)
        Set-JsonProperty -Object $artifact -Name "flashNum" -Value $defaultFlashNum
        if ($artifact.PSObject.Properties["offset"]) {
            $artifact.PSObject.Properties.Remove("offset")
        }
        $scannedBootloaders.Add([pscustomobject]@{
            DateTime = $stamp.DateTime
            Artifact = $artifact
        })
    }

    foreach ($oldArtifact in $existingBootloaders) {
        if (-not $seenBootloaderVersions.ContainsKey([string]$oldArtifact.version)) {
            $changeMessages.Add("REMOVE $($catalog.deviceId) bootloader: $($oldArtifact.version)")
        }
    }

    $orderedBootloaders = @($scannedBootloaders | Sort-Object DateTime | ForEach-Object {
        $_.Artifact
    })
    for ($index = 0; $index -lt $orderedBootloaders.Count; ++$index) {
        $artifact = $orderedBootloaders[$index]
        if ($index -eq ($orderedBootloaders.Count - 1)) {
            Set-JsonProperty -Object $artifact -Name "default" -Value $true
        } elseif ($artifact.PSObject.Properties["default"]) {
            $artifact.PSObject.Properties.Remove("default")
        }
    }
    $hasStandaloneArtifacts = ($null -ne $catalog.PSObject.Properties["artifacts"]) -or
        ($otherArtifacts.Count -gt 0) -or ($orderedBootloaders.Count -gt 0)
    if ($hasStandaloneArtifacts) {
        Set-JsonProperty -Object $catalog -Name "artifacts" -Value (
            @($otherArtifacts) + @($orderedBootloaders))
    }

    foreach ($artifact in @($catalog.artifacts | Where-Object {
        $null -ne $_ -and -not [string]::IsNullOrWhiteSpace([string]$_.relativePath)
    })) {
        $relativePath = [string]$artifact.relativePath
        if ($relativePath -notmatch '^flash/(?<path>.+)$') {
            throw "Artifact in catalog '$($catalog.deviceId)' has an unsupported relativePath '$relativePath'"
        }
        $artifactPath = Join-Path $FlashRoot ($Matches["path"] -replace '/', [IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Artifact in catalog '$($catalog.deviceId)' does not exist: $artifactPath"
        }
        $artifactFile = Get-Item -LiteralPath $artifactPath
        if (-not ($supportedExtensions -contains $artifactFile.Extension.ToLowerInvariant())) {
            throw "Artifact in catalog '$($catalog.deviceId)' uses unsupported format: $artifactPath"
        }
        $assignedFiles[$artifactFile.FullName.ToLowerInvariant()] = $true
        Set-JsonProperty -Object $artifact -Name "sha256" -Value (
            (Get-FileHash -LiteralPath $artifactFile.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
        )
        Set-JsonProperty -Object $artifact -Name "format" -Value $(
            if ($artifactFile.Extension -ieq ".hex") { "intelHex" } else { "binary" }
        )
    }
}

$unassignedFiles = @($allFirmwareFiles | Where-Object {
    -not $assignedFiles.ContainsKey($_.FullName.ToLowerInvariant())
})
if ($unassignedFiles.Count -gt 0) {
    throw "Firmware files outside configured device directories: $($unassignedFiles.FullName -join ', ')"
}

$newLine = if ($originalJson.Contains("`r`n")) { "`r`n" } else { "`n" }
$updatedJson = (ConvertTo-ProjectJson -Value $catalogRoot -NewLine $newLine) + $newLine
if ($updatedJson -ceq $originalJson) {
    Write-Host "Firmware catalog is already synchronized ($($allFirmwareFiles.Count) file(s))."
    return
}

if ($Check) {
    foreach ($message in $changeMessages) {
        Write-Host $message
    }
    throw "Firmware catalog is out of date. Run scripts\sync-firmware-config.ps1 without -Check."
}

[IO.File]::WriteAllText($CatalogPath, $updatedJson, $utf8NoBom)
foreach ($message in $changeMessages) {
    Write-Host $message
}
Write-Host "Updated firmware catalog '$CatalogPath' from $($allFirmwareFiles.Count) file(s)."
