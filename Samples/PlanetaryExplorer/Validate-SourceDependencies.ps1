[CmdletBinding()]
param(
    [string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer',
    [string]$SourceProject = 'D:/Projects/KeireValidation/EarthSpaceFlight'
)

$ErrorActionPreference = 'Stop'

function ConvertTo-AssetIdentifier
{
    param([string]$Identifier, [string]$Context)

    if ($Identifier -notmatch '^(?:[0-9a-fA-F]{32}|[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12})$')
    {
        throw "Malformed asset identifier '$Identifier' in $Context"
    }

    return ([Guid]::Parse($Identifier)).ToString('D')
}

$known = @{}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $SourceProject 'Assets') -Filter '*.keiremeta' -File -Recurse)
{
    $metadata = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    $identifier = ConvertTo-AssetIdentifier $metadata.id $file.FullName
    $known[$identifier] = $file.FullName
}
$present = @{}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Project 'Assets') -Filter '*.keiremeta' -File -Recurse)
{
    $metadata = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    $assetIdentifier = ConvertTo-AssetIdentifier $metadata.id $file.FullName
    ConvertTo-AssetIdentifier $metadata.type $file.FullName | Out-Null
    foreach ($dependency in $metadata.dependencies)
    {
        ConvertTo-AssetIdentifier $dependency $file.FullName | Out-Null
    }
    if ($present.ContainsKey($assetIdentifier))
    {
        throw "Duplicate asset identifier '$($metadata.id)' in $($file.FullName)"
    }
    $present[$assetIdentifier] = $file.FullName
    $source = $file.FullName.Substring(0, $file.FullName.Length - '.keiremeta'.Length)
    if (-not (Test-Path -LiteralPath $source -PathType Leaf))
    {
        throw "Metadata lacks source: $source"
    }
}
$references = 0
foreach ($folder in @('Assets', 'ProjectSettings'))
{
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Project $folder) -File -Recurse)
    {
        if ($file.Extension -in @('.png', '.jpg', '.jpeg', '.fbx', '.glb', '.wav', '.ogg'))
        {
            continue
        }
        $identifierPattern = '(?<![0-9a-fA-F])(?:[0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}|[0-9a-fA-F]{32})(?![0-9a-fA-F])'
        foreach ($match in [regex]::Matches((Get-Content -LiteralPath $file.FullName -Raw), $identifierPattern))
        {
            $identifier = ConvertTo-AssetIdentifier $match.Value $file.FullName
            if ($known.ContainsKey($identifier))
            {
                $references++
                if (-not $present.ContainsKey($identifier))
                {
                    throw "Missing known source asset $($match.Value) referenced by $($file.FullName)"
                }
            }
        }
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $Project 'ProjectSettings/Project.keireproject') -Raw | ConvertFrom-Json
$startupIdentifier = ConvertTo-AssetIdentifier $manifest.startupScene 'startup scene'
if (-not $present.ContainsKey($startupIdentifier))
{
    throw 'Startup scene is absent.'
}
Write-Output "Validated $references known asset references and $($present.Count) authored metadata/source pairs. Unknown engine/component IDs and runtime behavior require engine validation."
