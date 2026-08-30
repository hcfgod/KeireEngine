[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ProjectRoot
)

$ErrorActionPreference = 'Stop'
$projectPath = [System.IO.Path]::GetFullPath($ProjectRoot)
$featureRoot = Join-Path $projectPath 'Assets\Examples\FeatureGallery'
$shaderTemplate = Join-Path $featureRoot 'Materials\ShaderGraphs\01_Foundations\SG_02_TiledCeramic.keireshadergraph'
$materialTemplate = Join-Path $featureRoot 'Materials\MaterialGraphs\01_Foundations\MG_02_TiledCeramic.keirematerialgraph'
$vfxRoot = Join-Path $featureRoot 'VFX'
$destinationRoot = Join-Path $featureRoot 'SyntyWar'

foreach ($required in @($shaderTemplate, $materialTemplate, $vfxRoot))
{
    if (-not (Test-Path -LiteralPath $required))
    {
        throw "The Feature Gallery template is missing: $required"
    }
}

function Get-StableGuid
{
    param([string] $Namespace, [string] $Value)

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try
    {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("$Namespace`n$Value")
        $hex = [System.Convert]::ToHexString($sha.ComputeHash($bytes)).ToLowerInvariant()
        return '{0}-{1}-5{2}-{3}{4}-{5}' -f $hex.Substring(0, 8), $hex.Substring(8, 4),
            $hex.Substring(13, 3), @('8', '9', 'a', 'b')[[Convert]::ToInt32($hex.Substring(16, 1), 16) % 4],
            $hex.Substring(17, 3), $hex.Substring(20, 12)
    }
    finally
    {
        $sha.Dispose()
    }
}

function Copy-JsonValue
{
    param($Value)
    return ($Value | ConvertTo-Json -Depth 100 | ConvertFrom-Json)
}

function Get-OwnedGuidMap
{
    param($Document, [string] $Seed)

    $map = @{}
    function Visit($Value)
    {
        if ($null -eq $Value)
        {
            return
        }
        if ($Value -is [System.Collections.IEnumerable] -and $Value -isnot [string] -and
            $Value -isnot [System.Management.Automation.PSCustomObject])
        {
            foreach ($item in $Value)
            {
                Visit $item
            }
            return
        }
        if ($Value -isnot [System.Management.Automation.PSCustomObject])
        {
            return
        }
        foreach ($property in $Value.PSObject.Properties)
        {
            if ($property.Name -eq 'id' -and $property.Value -is [string] -and
                $property.Value -match '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$')
            {
                $map[$property.Value] = Get-StableGuid $Seed $property.Value
            }
            Visit $property.Value
        }
    }
    Visit $Document
    return $map
}

function Replace-JsonStrings
{
    param($Value, [hashtable] $Replacements)

    if ($null -eq $Value)
    {
        return
    }
    if ($Value -is [System.Collections.IList])
    {
        for ($index = 0; $index -lt $Value.Count; ++$index)
        {
            if ($Value[$index] -is [string] -and $Replacements.ContainsKey($Value[$index]))
            {
                $Value[$index] = $Replacements[$Value[$index]]
            }
            else
            {
                Replace-JsonStrings $Value[$index] $Replacements
            }
        }
        return
    }
    if ($Value -isnot [System.Management.Automation.PSCustomObject])
    {
        return
    }
    foreach ($property in $Value.PSObject.Properties)
    {
        if ($property.Value -is [string] -and $Replacements.ContainsKey($property.Value))
        {
            $property.Value = $Replacements[$property.Value]
        }
        else
        {
            Replace-JsonStrings $property.Value $Replacements
        }
    }
}

function Set-NamedGraphValue
{
    param($Document, [string] $Name, $Value)

    foreach ($node in @($Document.nodes) + @($Document.surfaceGraph.nodes))
    {
        if ($null -eq $node)
        {
            continue
        }
        if ($node.name -match $Name -or $node.symbol -match $Name)
        {
            if ($node.PSObject.Properties.Name -contains 'value')
            {
                $node.value = Copy-JsonValue $Value
            }
        }
    }
    foreach ($property in @($Document.properties))
    {
        if ($property.name -match $Name)
        {
            $property.value = Copy-JsonValue $Value
        }
    }
}

function Write-JsonFile
{
    param([string] $Path, $Document)

    $directory = Split-Path -Parent $Path
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    $json = $Document | ConvertTo-Json -Depth 100
    [System.IO.File]::WriteAllText($Path, $json + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false))
}

function Write-AssetMeta
{
    param(
        [string] $Path,
        [string] $Id,
        [string] $Importer,
        [int] $ImporterVersion,
        [string] $Type,
        [string[]] $Dependencies
    )

    $document = [ordered]@{
        dependencies = @($Dependencies)
        id = $Id
        importer = $Importer
        importerVersion = $ImporterVersion
        schemaVersion = 1
        subAssets = @()
        type = $Type
    }
    Write-JsonFile "$Path.keiremeta" $document
}

$shaderSource = Get-Content -LiteralPath $shaderTemplate -Raw | ConvertFrom-Json
$materialSource = Get-Content -LiteralPath $materialTemplate -Raw | ConvertFrom-Json
$templateShaderId = '41d75e64-f977-52fe-82c0-e2e5a852309d'
$templateTextureId = 'b2c4f70a-7f2f-47f0-b83e-a554a0bda703'
$materials = @(
    @{ Key = 'InfantryAtlas'; Name = 'Infantry Atlas'; Texture = '1184aae8-a8c5-4334-ab3c-5a0eef7811ca'; Tiling = @(1.0, 1.0); Roughness = 0.58 },
    @{ Key = 'GermanArmor'; Name = 'German Armor'; Texture = '58ce409c-750c-434b-9916-fa0fa594569f'; Tiling = @(1.0, 1.0); Roughness = 0.38 },
    @{ Key = 'AmericanWarbird'; Name = 'American Warbird'; Texture = 'd1ba40ac-84c5-4e8a-a47b-c29ef7504cba'; Tiling = @(1.0, 1.0); Roughness = 0.32 },
    @{ Key = 'BattlefieldRubble'; Name = 'Battlefield Rubble'; Texture = '439221d6-7494-4ff7-b092-5a782116b2dc'; Tiling = @(4.0, 4.0); Roughness = 0.88 },
    @{ Key = 'PropagandaPoster'; Name = 'Propaganda Poster'; Texture = '1342c66b-308e-47c4-87de-178bebfea98f'; Tiling = @(1.0, 1.0); Roughness = 0.64 }
)

$shaderDirectory = Join-Path $destinationRoot 'Shaders'
$materialDirectory = Join-Path $destinationRoot 'Materials'
foreach ($entry in $materials)
{
    $shader = Copy-JsonValue $shaderSource
    $shaderMap = Get-OwnedGuidMap $shader "synty-war-shader-$($entry.Key)"
    $shaderMap[$templateTextureId] = $entry.Texture
    Replace-JsonStrings $shader $shaderMap
    Set-NamedGraphValue $shader 'Base Texture' $entry.Texture
    Set-NamedGraphValue $shader 'Tiling' $entry.Tiling
    Set-NamedGraphValue $shader 'Roughness' $entry.Roughness
    foreach ($node in $shader.nodes)
    {
        if ($node.name -eq 'Base Texture')
        {
            $node.name = "$($entry.Name) Texture"
            $node.parameterMetadata.description = "Imported Synty texture used by the $($entry.Name) shader."
        }
    }
    $shaderId = Get-StableGuid 'synty-war-shader-asset' $entry.Key
    $shaderPath = Join-Path $shaderDirectory "SG_$($entry.Key).keireshadergraph"
    Write-JsonFile $shaderPath $shader
    Write-AssetMeta $shaderPath $shaderId 'Keire.ShaderGraph' 19 '4b454952-4553-4752-4150-480000000001' @()

    $material = Copy-JsonValue $materialSource
    $materialMap = Get-OwnedGuidMap $material "synty-war-material-$($entry.Key)"
    $materialMap[$templateShaderId] = $shaderId
    $materialMap[$templateTextureId] = $entry.Texture
    Replace-JsonStrings $material $materialMap
    $material.shader.asset = $shaderId
    Set-NamedGraphValue $material 'Base Texture' $entry.Texture
    Set-NamedGraphValue $material 'Tiling' $entry.Tiling
    Set-NamedGraphValue $material 'Roughness' $entry.Roughness
    foreach ($node in @($material.nodes) + @($material.surfaceGraph.nodes))
    {
        if ($null -ne $node -and $node.name -match 'Base Texture')
        {
            $node.name = $node.name -replace 'Base Texture', "$($entry.Name) Texture"
        }
    }
    $materialId = Get-StableGuid 'synty-war-material-asset' $entry.Key
    $materialPath = Join-Path $materialDirectory "MG_$($entry.Key).keirematerialgraph"
    Write-JsonFile $materialPath $material
    Write-AssetMeta $materialPath $materialId 'Keire.MaterialGraph' 9 '4b454952-454d-4752-4150-480000000001' @($shaderId)
}

function Set-VfxModuleValues
{
    param($Document, [string] $Type, [hashtable] $Values)

    foreach ($module in @($Document.modules | Where-Object { $_.type -eq $Type }))
    {
        foreach ($key in $Values.Keys)
        {
            if ($module.PSObject.Properties.Name -contains $key)
            {
                $module.$key = Copy-JsonValue $Values[$key]
            }
        }
    }
    foreach ($system in $Document.systems)
    {
        foreach ($node in $system.nodes)
        {
            foreach ($block in @($node.blocks | Where-Object { $_.type -eq $Type }))
            {
                foreach ($pin in $block.pins)
                {
                    if ($Values.ContainsKey($pin.semantic))
                    {
                        $pin.default = Copy-JsonValue $Values[$pin.semantic]
                    }
                }
            }
        }
    }
}

function Set-VfxGradient
{
    param($Document, [object[]] $Start, [object[]] $End)

    foreach ($module in @($Document.modules | Where-Object { $_.type -eq 'colorOverLifetime' }))
    {
        $module.gradient.keys[0].color = Copy-JsonValue $Start
        $module.gradient.keys[-1].color = Copy-JsonValue $End
    }
    Set-VfxModuleValues $Document 'colorOverLifetime' @{ color = $Start }
}

function Set-VfxSize
{
    param($Document, [double] $Start, [double] $End)

    foreach ($module in @($Document.modules | Where-Object { $_.type -eq 'sizeOverLifetime' }))
    {
        $module.curve[0].value = $Start
        $module.curve[-1].value = $End
    }
    Set-VfxModuleValues $Document 'sizeOverLifetime' @{ size = $Start }
}

$effects = @(
    @{
        Key = 'ArtilleryImpact'; Name = 'Artillery Impact'; Template = 'ArcaneNova.keirevfx'; Capacity = 2048; Duration = 2.2; Loop = $false; Seed = 1944
        Values = @{
            burst = @{ count = 128; cycles = 4; interval = 0.05; time = 0.0 }
            shape = @{ shape = 'sphere'; radius = 0.35; coneAngleDegrees = 42.0; coneLength = 1.4 }
            initialize = @{ lifetimeMinimum = 0.6; lifetimeMaximum = 2.0; velocityMinimum = @(-8.0, 1.0, -8.0); velocityMaximum = @(8.0, 12.0, 8.0); rotationMinimum = @(0.0, 0.0, -180.0); rotationMaximum = @(0.0, 0.0, 180.0) }
            force = @{ force = @(1.4, -5.5, 0.3); gravityMultiplier = 1.1 }
            renderer = @{ renderer = 'sprite'; sprite = ''; mesh = '' }
        }; Size = @(0.45, 0.03); StartColor = @(1.0, 0.7, 0.16, 1.0); EndColor = @(0.16, 0.035, 0.01, 0.0)
    },
    @{
        Key = 'TankTracerSalvo'; Name = 'Tank Tracer Salvo'; Template = 'RibbonTrail.keirevfx'; Capacity = 2048; Duration = 0.8; Loop = $false; Seed = 1945
        Values = @{
            burst = @{ count = 54; cycles = 6; interval = 0.035; time = 0.0 }
            shape = @{ shape = 'cone'; radius = 0.025; coneAngleDegrees = 1.2; coneLength = 0.15 }
            initialize = @{ lifetimeMinimum = 0.18; lifetimeMaximum = 0.42; velocityMinimum = @(-0.12, -0.12, 42.0); velocityMaximum = @(0.12, 0.12, 68.0); rotationMinimum = @(0.0, 0.0, 0.0); rotationMaximum = @(0.0, 0.0, 0.0) }
            force = @{ force = @(0.0, -0.2, 0.0); gravityMultiplier = 0.05 }
            renderer = @{ renderer = 'ribbon'; sprite = ''; mesh = '' }
        }; Size = @(0.065, 0.012); StartColor = @(1.0, 0.94, 0.48, 1.0); EndColor = @(1.0, 0.08, 0.01, 0.0)
    },
    @{
        Key = 'BurningWreckPlume'; Name = 'Burning Wreck Plume'; Template = 'VolumetricFog.keirevfx'; Capacity = 4096; Duration = 8.0; Loop = $true; Seed = 1946
        Values = @{
            emissionRate = @{ particlesPerSecond = 54.0 }
            shape = @{ shape = 'box'; boxHalfExtent = @(1.6, 0.25, 1.3); radius = 0.5 }
            initialize = @{ lifetimeMinimum = 3.5; lifetimeMaximum = 7.0; velocityMinimum = @(-0.35, 0.65, -0.35); velocityMaximum = @(0.55, 2.4, 0.55); rotationMinimum = @(0.0, 0.0, -180.0); rotationMaximum = @(0.0, 0.0, 180.0) }
            force = @{ force = @(0.42, 0.12, 0.08); gravityMultiplier = -0.08 }
            renderer = @{ renderer = 'volumetric'; sprite = ''; mesh = '' }
        }; Size = @(1.2, 4.8); StartColor = @(0.34, 0.27, 0.21, 0.72); EndColor = @(0.055, 0.06, 0.065, 0.0)
    },
    @{
        Key = 'BattlefieldDustGust'; Name = 'Battlefield Dust Gust'; Template = 'SpectralMist.keirevfx'; Capacity = 4096; Duration = 6.0; Loop = $true; Seed = 1947
        Values = @{
            emissionRate = @{ particlesPerSecond = 72.0 }
            shape = @{ shape = 'box'; boxHalfExtent = @(5.0, 0.3, 2.2); radius = 0.5 }
            initialize = @{ lifetimeMinimum = 2.5; lifetimeMaximum = 6.0; velocityMinimum = @(1.8, 0.05, -0.45); velocityMaximum = @(5.2, 0.65, 0.65); rotationMinimum = @(0.0, 0.0, -45.0); rotationMaximum = @(0.0, 0.0, 45.0) }
            force = @{ force = @(0.75, 0.02, 0.18); gravityMultiplier = 0.0 }
            renderer = @{ renderer = 'sprite'; sprite = ''; mesh = '' }
        }; Size = @(1.6, 3.6); StartColor = @(0.62, 0.48, 0.30, 0.45); EndColor = @(0.25, 0.18, 0.11, 0.0)
    },
    @{
        Key = 'SignalFlare'; Name = 'Signal Flare'; Template = 'ForgeSparks.keirevfx'; Capacity = 2048; Duration = 3.5; Loop = $false; Seed = 1948
        Values = @{
            emissionRate = @{ particlesPerSecond = 35.0 }
            burst = @{ count = 84; cycles = 3; interval = 0.12; time = 0.0 }
            shape = @{ shape = 'cone'; radius = 0.12; coneAngleDegrees = 8.0; coneLength = 0.5 }
            initialize = @{ lifetimeMinimum = 1.4; lifetimeMaximum = 3.2; velocityMinimum = @(-0.45, 7.5, -0.45); velocityMaximum = @(0.45, 13.0, 0.45); rotationMinimum = @(0.0, 0.0, -180.0); rotationMaximum = @(0.0, 0.0, 180.0) }
            force = @{ force = @(0.3, -1.8, 0.12); gravityMultiplier = 0.35 }
            renderer = @{ renderer = 'sprite'; sprite = ''; mesh = '' }
        }; Size = @(0.28, 0.025); StartColor = @(0.18, 1.0, 0.62, 1.0); EndColor = @(0.02, 0.12, 0.06, 0.0)
    }
)

$effectDirectory = Join-Path $destinationRoot 'VFX'
foreach ($entry in $effects)
{
    $sourcePath = Join-Path $vfxRoot $entry.Template
    $effect = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json
    $ownedIds = Get-OwnedGuidMap $effect "synty-war-vfx-$($entry.Key)"
    Replace-JsonStrings $effect $ownedIds
    $effect.name = $entry.Name
    $effect.capacity = $entry.Capacity
    $effect.duration = $entry.Duration
    $effect.loop = $entry.Loop
    $effect.seed = $entry.Seed
    foreach ($moduleType in $entry.Values.Keys)
    {
        Set-VfxModuleValues $effect $moduleType $entry.Values[$moduleType]
    }
    Set-VfxSize $effect $entry.Size[0] $entry.Size[1]
    Set-VfxGradient $effect $entry.StartColor $entry.EndColor
    $effectPath = Join-Path $effectDirectory "$($entry.Key).keirevfx"
    Write-JsonFile $effectPath $effect
    $effectId = Get-StableGuid 'synty-war-vfx-asset' $entry.Key
    Write-AssetMeta $effectPath $effectId 'Keire.VfxEffect' 5 '4b454952-4556-4658-4546-464543540001' @()
}

$scriptDirectory = Join-Path $destinationRoot 'Scripts'
[System.IO.Directory]::CreateDirectory($scriptDirectory) | Out-Null
$scripts = @{
    'BattlefieldVfxDirector.cs' = @'
using System.Collections;
using Keire;

namespace FeatureGallery.SyntyWar;

/// <summary>Cycles imported battlefield effects and demonstrates typed VFX and coroutine control.</summary>
[StableComponentId("6b127ca6-5198-5f2d-9c8b-6cd6a9f5bb1e")]
public sealed class BattlefieldVfxDirector : Behaviour
{
    [SerializeField, StableFieldId("42d9a6f8-a978-5f4e-afdd-3637ed113b50")]
    private VfxEffect? _artilleryImpact;

    [SerializeField, StableFieldId("c943d8ae-66e5-5f21-8edf-72ebaf2ad954")]
    private VfxEffect? _tankTracerSalvo;

    [SerializeField, StableFieldId("c0e38314-bf5b-5551-972e-e13b99e70bd4")]
    private VfxEffect? _burningWreckPlume;

    [SerializeField, StableFieldId("67379fca-02dc-5dbb-9c66-12c24cdb2441")]
    private VfxEffect? _battlefieldDustGust;

    [SerializeField, StableFieldId("34a815ca-769d-59f5-8de5-b5197aa48f73")]
    private VfxEffect? _signalFlare;

    [SerializeField, StableFieldId("03c51125-ae70-56df-a676-4ffb42ca04c2")]
    [Range(0.1, 10.0), Tooltip("Seconds between showcase effects.")]
    private float _effectInterval = 1.4f;

    [HotReloadState] private int _nextEffect;
    private Coroutine _sequence;

    protected override void Start() => _sequence = StartCoroutine(PlayBattlefieldSequence());

    protected override void Update()
    {
        if (Keyboard.Current?.spaceKey.WasPressedThisFrame == true)
            PlayNextEffect();
    }

    public void RestartSequence()
    {
        StopCoroutine(_sequence);
        _sequence = StartCoroutine(PlayBattlefieldSequence());
    }

    private IEnumerator PlayBattlefieldSequence()
    {
        while (true)
        {
            PlayNextEffect();
            yield return new WaitForSeconds(_effectInterval);
        }
    }

    private void PlayNextEffect()
    {
        VfxEffect?[] effects =
        [
            _artilleryImpact,
            _tankTracerSalvo,
            _burningWreckPlume,
            _battlefieldDustGust,
            _signalFlare
        ];
        VfxEffect? effect = effects[_nextEffect++ % effects.Length];
        if (effect is not null && effect.IsValid)
            _ = Vfx.Play(Entity, effect, restart: true);
    }

    protected override void OnBeforeReload() => StopAllCoroutines();

    protected override void OnDisable()
    {
        StopAllCoroutines();
        _ = Vfx.Stop(Entity);
    }
}
'@
    'SyntyMaterialWeathering.cs' = @'
using System.Collections;
using Keire;

namespace FeatureGallery.SyntyWar;

/// <summary>Animates graph parameters without cloning the shared Synty material asset.</summary>
[StableComponentId("0177eafd-ff33-52b9-b285-c4206a6c5b34")]
public sealed class SyntyMaterialWeathering : Behaviour
{
    [SerializeField, StableFieldId("93412ee8-a61f-58da-bf74-9ea1a78f7c41")]
    [Range(0.0, 1.0)]
    private float _dryRoughness = 0.38f;

    [SerializeField, StableFieldId("b3e09b6a-30b7-5ce9-b784-a65599014483")]
    [Range(0.0, 1.0)]
    private float _wetRoughness = 0.08f;

    [SerializeField, StableFieldId("fc58e95c-8998-56c2-a05a-9ac85ca5660e")]
    [Range(0.1, 8.0)]
    private float _transitionSeconds = 2.5f;

    [HotReloadState] private float _wetness;
    private MeshRenderer? _renderer;
    private Coroutine _weatherCycle;

    protected override void Start() => Initialize();
    protected override void OnAfterReload() => Initialize();

    private void Initialize()
    {
        _renderer = GetComponent<MeshRenderer>();
        _weatherCycle = StartCoroutine(WeatherCycle());
    }

    private IEnumerator WeatherCycle()
    {
        while (true)
        {
            yield return FadeWetness(1.0f);
            yield return new WaitForSeconds(0.6f);
            yield return FadeWetness(0.0f);
            yield return new WaitForSecondsRealtime(0.35f);
        }
    }

    private IEnumerator FadeWetness(float target)
    {
        while (MathF.Abs(_wetness - target) > 0.001f)
        {
            float direction = MathF.Sign(target - _wetness);
            _wetness = Math.Clamp(_wetness + direction * Time.DeltaTime / MathF.Max(0.1f, _transitionSeconds), 0.0f, 1.0f);
            ApplyMaterialOverrides();
            yield return null;
        }
    }

    private void ApplyMaterialOverrides()
    {
        float roughness = _dryRoughness + ((_wetRoughness - _dryRoughness) * _wetness);
        _renderer?.PropertyBlock.SetFloat("Roughness", roughness);
        _renderer?.PropertyBlock.SetVector("Tiling", new Vector2(1.0f + _wetness * 0.05f, 1.0f));
    }

    protected override void OnBeforeReload() => StopCoroutine(_weatherCycle);

    protected override void OnDisable()
    {
        StopAllCoroutines();
        _renderer?.PropertyBlock.Clear();
    }
}
'@
    'BattlefieldRuntimeTour.cs' = @'
using Keire;

namespace FeatureGallery.SyntyWar;

/// <summary>Demonstrates direct input, jobs, preferences, physics queries, debug drawing, and reload-safe state.</summary>
[StableComponentId("668a1237-75f0-5f4d-bb4b-73f05fa440a4")]
public sealed class BattlefieldRuntimeTour : Behaviour
{
    [SerializeField, StableFieldId("e8bb116c-1753-52b9-952c-0cc75bfcde9f")]
    [Range(1.0, 250.0)]
    private float _probeDistance = 40.0f;

    [HotReloadState] private int _completedScans;
    [HotReloadState] private float _elapsed;
    private Job? _scanJob;

    protected override void Start()
    {
        _completedScans = PlayerPreferences.GetInt("SyntyWar.CompletedScans", _completedScans);
        _ = RunBackgroundScanAsync();
    }

    protected override void Update()
    {
        _elapsed += Time.DeltaTime;
        Keyboard? keyboard = Keyboard.Current;
        if (keyboard?.rKey.WasPressedThisFrame == true)
            _ = RunBackgroundScanAsync();

        Vector3 start = new(0.0f, 2.0f, 0.0f);
        Vector3 end = start + new Vector3(0.0f, 0.0f, _probeDistance);
        Debug.DrawLine(start, end, new Color(0.2f, 0.9f, 0.45f, 1.0f));
        if (Physics.TryRaycast(Entity, start, new Vector3(0.0f, 0.0f, 1.0f), out RaycastHit hit, _probeDistance))
            Debug.DrawLine(start, hit.Point, new Color(1.0f, 0.2f, 0.08f, 1.0f));
    }

    private async Task RunBackgroundScanAsync()
    {
        if (_scanJob is { Status: JobStatus.Waiting or JobStatus.Running })
            return;

        try
        {
            double checksum = 0.0;
            _scanJob = Jobs.Submit(
                context =>
                {
                    for (int index = 0; index < 8192; ++index)
                    {
                        context.CancellationToken.ThrowIfCancellationRequested();
                        checksum += Math.Sin(index * 0.0025);
                    }
                },
                new JobDescription
                {
                    Name = "Synty battlefield visibility probe",
                    Priority = JobPriority.Low,
                    Class = JobClass.Compute
                });
            using CancellationTokenRegistration registration = LifetimeToken.Register(() => _scanJob.Cancel());
            await _scanJob.Completion;
            ++_completedScans;
            PlayerPreferences.SetInt("SyntyWar.CompletedScans", _completedScans);
            Debug.Log($"Battlefield scan {_completedScans} completed with checksum {checksum:F3}.");
        }
        catch (OperationCanceledException) when (LifetimeToken.IsCancellationRequested)
        {
        }
        catch (Exception error)
        {
            Debug.LogException(error);
        }
    }

    protected override void OnDisable()
    {
        if (_scanJob is { Status: JobStatus.Waiting or JobStatus.Running })
            _scanJob.Cancel();
    }
}
'@
}
foreach ($script in $scripts.GetEnumerator())
{
    [System.IO.File]::WriteAllText((Join-Path $scriptDirectory $script.Key), $script.Value + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false))
}

$readme = @'
# Synty War feature showcase

This folder is generated from the real `POLYGON_War_SourceFiles_v4` imports in this project.

- `Shaders` contains five independently editable texture-sampling Shader Graphs.
- `Materials` binds those graphs to infantry, armor, aircraft, rubble, and poster textures imported from the Synty pack.
- `VFX` contains an artillery impact, tracer salvo, burning-wreck plume, battlefield dust gust, and signal flare.
- `Scripts` contains runtime examples for coroutine sequencing, material overrides, VFX control, reload-safe state, and jobs.

The imported Synty files remain governed by their original license and are not part of the Kéire source distribution.
'@
[System.IO.Directory]::CreateDirectory($destinationRoot) | Out-Null
[System.IO.File]::WriteAllText((Join-Path $destinationRoot 'README.md'), $readme + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))

Write-Host "Created the Synty War showcase under $destinationRoot"
