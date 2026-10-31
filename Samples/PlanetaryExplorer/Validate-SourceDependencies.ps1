[CmdletBinding()]
param(
    [string]$Project = 'D:/Projects/KeireValidation/PlanetaryExplorer',
    [string]$SourceProject = 'D:/Projects/KeireValidation/EarthSpaceFlight'
)

$ErrorActionPreference = 'Stop'
$known = @{}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $SourceProject 'Assets') -Filter '*.keiremeta' -File -Recurse)
{
    $metadata = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    $known[[string]$metadata.id] = $file.FullName
}
$present = @{}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Project 'Assets') -Filter '*.keiremeta' -File -Recurse)
{
    $metadata = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    $present[[string]$metadata.id] = $file.FullName
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
        foreach ($match in [regex]::Matches((Get-Content -LiteralPath $file.FullName -Raw), '[0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}'))
        {
            if ($known.ContainsKey($match.Value))
            {
                $references++
                if (-not $present.ContainsKey($match.Value))
                {
                    throw "Missing known source asset $($match.Value) referenced by $($file.FullName)"
                }
            }
        }
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $Project 'ProjectSettings/Project.keireproject') -Raw | ConvertFrom-Json
if (-not $present.ContainsKey([string]$manifest.startupScene))
{
    throw 'Startup scene is absent.'
}
Write-Output "Validated $references known asset references and $($present.Count) authored metadata/source pairs. Unknown engine/component IDs and runtime behavior require engine validation."
