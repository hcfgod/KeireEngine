<#
.SYNOPSIS
Inspects the actual compiled DXIL resource and signature contract on Windows.
.DESCRIPTION
Builds a small standalone DXC disassembly helper, using the repository shader compiler DLL.
Writes disassembly and JSON under Build/Validation by default. With -Reference, rejects changes
in resource bindings or declared input/output signatures. Compiler-local resource IDs and
used-component flags are excluded from comparison; source metadata is never used as evidence.
.PARAMETER VisualStudioMajorVersion
Select 17 for Visual Studio 2022 or 18 for Visual Studio 2026 when building the helper.
.EXAMPLE
./Scripts/Validation/inspect-dxil.ps1 -Shader new.dxil -Reference baseline.dxil
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Shader,
    [string]$Reference = '',
    [string]$OutputDirectory = '',
    [string]$CompilerLibrary = '',
    [ValidateSet(17, 18)][int]$VisualStudioMajorVersion = 18
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $repositoryRoot 'Scripts/Windows/common.ps1')
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $repositoryRoot 'Build/Validation/DxilReflection'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
if (!$CompilerLibrary) {
    $CompilerLibrary = Join-Path $repositoryRoot 'Build/Tools/ShaderCompiler/dxcompiler.dll'
}
$CompilerLibrary = (Resolve-Path -LiteralPath $CompilerLibrary).Path
$source = Join-Path $PSScriptRoot 'dxil-disassemble.cpp'
$executable = Join-Path $OutputDirectory 'dxil-disassemble.exe'
if (!(Test-Path -LiteralPath $executable) -or
    (Get-Item -LiteralPath $source).LastWriteTimeUtc -gt (Get-Item -LiteralPath $executable).LastWriteTimeUtc) {
    $null = Enter-VSDeveloperEnvironment -MajorVersion $VisualStudioMajorVersion -Architecture x64
    $include = Join-Path $repositoryRoot 'Vendor/SDL_shadercross/external/DirectXShaderCompiler/include/dxc'
    & cl.exe /nologo /std:c++20 /EHsc /W4 /O2 "/I$include" $source `
        "/Fo$OutputDirectory/dxil-disassemble.obj" "/Fe$executable" /link "/PDB:$OutputDirectory/dxil-disassemble.pdb"
    if ($LASTEXITCODE -ne 0) {
        throw "DXIL inspection helper build failed with exit code $LASTEXITCODE."
    }
}

function Get-TableRows {
    param([string[]]$Lines, [string]$Section)
    $insideSection = $false
    $hasRows = $false
    foreach ($line in $Lines) {
        if ($line -eq "; ${Section}:") {
            $insideSection = $true
            continue
        }
        if (!$insideSection) { continue }
        if ($line -match '^;\s*$') {
            if ($hasRows) { break }
            continue
        }
        $row = ($line -replace '^;\s*', '').Trim()
        if ($row -match '^(Name\s|---)') { continue }
        $hasRows = $true
        $row -replace '\s+', ' '
    }
}

function Get-ShaderReflection {
    param([string]$Path, [string]$Label)
    $resolvedShader = (Resolve-Path -LiteralPath $Path).Path
    $disassemblyPath = Join-Path $OutputDirectory "$Label.disassembly.txt"
    & $executable $CompilerLibrary $resolvedShader $disassemblyPath
    if ($LASTEXITCODE -ne 0) {
        throw "DXIL inspection failed for $resolvedShader."
    }
    $lines = Get-Content -LiteralPath $disassemblyPath
    $reflection = [ordered]@{
        Shader = $resolvedShader
        SHA256 = (Get-FileHash -LiteralPath $resolvedShader -Algorithm SHA256).Hash
        InputSignature = @(Get-TableRows -Lines $lines -Section 'Input signature')
        OutputSignature = @(Get-TableRows -Lines $lines -Section 'Output signature')
        ResourceBindings = @(Get-TableRows -Lines $lines -Section 'Resource Bindings')
    }
    if ($reflection.InputSignature.Count -eq 0 -or $reflection.OutputSignature.Count -eq 0) {
        throw "DXIL has no graphics input/output signature: $resolvedShader."
    }
    $reflection | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory "$Label.reflection.json") -Encoding utf8
    return [pscustomobject]$reflection
}

$actual = Get-ShaderReflection -Path $Shader -Label 'actual'
if ($Reference) {
    $expected = Get-ShaderReflection -Path $Reference -Label 'reference'
    $differences = @()
    foreach ($section in @('InputSignature', 'OutputSignature', 'ResourceBindings')) {
        # Internal resource IDs and used-component flags are compiler implementation details.
        # Preserve declared signature masks/registers and resource names/types/slots/spaces/counts.
        $normalize = if ($section -eq 'ResourceBindings') {
            { param($row) $row -replace ' (?:CB|S|T|U)\d+ ', ' ' }
        } else {
            { param($row) (($row -split ' ')[0..5]) -join ' ' }
        }
        $expectedRows = @($expected.$section | ForEach-Object { & $normalize $_ } | Sort-Object)
        $actualRows = @($actual.$section | ForEach-Object { & $normalize $_ } | Sort-Object)
        $difference = @(Compare-Object -ReferenceObject $expectedRows -DifferenceObject $actualRows)
        if ($difference.Count -gt 0) {
            $differences += [pscustomobject]@{ Section = $section; Differences = $difference }
        }
    }
    if ($differences.Count -gt 0) {
        $differences | ConvertTo-Json -Depth 6 | Write-Output
        throw 'Compiled DXIL binding/signature contract differs from reference.'
    }
    Write-Output 'Compiled DXIL binding/signature contract matches reference.'
}
$actual
