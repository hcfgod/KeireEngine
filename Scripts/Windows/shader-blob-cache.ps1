# Per-translation-unit cache. Callers retain their whole-generated-output publication lock.
. (Join-Path $PSScriptRoot 'generated-content-cache.ps1')

function Get-ShaderIncludeClosure
{
    param([string[]] $Sources, [string[]] $IncludeDirectories)
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $pending = [Collections.Generic.Stack[string]]::new()
    foreach ($source in $Sources)
    {
        $pending.Push([IO.Path]::GetFullPath($source))
    }
    while ($pending.Count -gt 0)
    {
        $path = $pending.Pop()
        if (-not $seen.Add($path))
        {
            continue
        }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf))
        {
            throw "Missing shader input: $path"
        }
        $text = [IO.File]::ReadAllText($path)
        $text = [regex]::Replace($text, '(?s)/\*.*?\*/', '')
        foreach ($directive in [regex]::Matches($text, '(?m)^\s*#\s*include\s+([^\r\n]+)'))
        {
            $include = [regex]::Match($directive.Groups[1].Value, '^"([^"\r\n]+)"\s*(?://.*)?$')
            if (-not $include.Success)
            {
                throw "Unsupported shader include in ${path}: $($directive.Value)"
            }
            $resolved = $null
            foreach ($directory in @((Split-Path -Parent $path)) + $IncludeDirectories)
            {
                $candidate = [IO.Path]::GetFullPath((Join-Path $directory $include.Groups[1].Value))
                if (Test-Path -LiteralPath $candidate -PathType Leaf)
                {
                    $resolved = $candidate
                    break
                }
            }
            if (-not $resolved)
            {
                throw "Unresolved shader include in ${path}: $($include.Groups[1].Value)"
            }
            $pending.Push($resolved)
        }
    }
    return @($seen | Sort-Object)
}

function Get-ShaderCompilerInputs
{
    param([string] $Compiler)
    return @([IO.Path]::GetFullPath($Compiler)) + @(
        Get-ChildItem -LiteralPath (Split-Path -Parent $Compiler) -File -Filter '*.dll' |
            Sort-Object Name | ForEach-Object FullName)
}

function Get-ShaderBlobInputIdentity
{
    param([string] $Source, [string[]] $IncludeDirectories, [string] $CompilerIdentity)
    $closure = @(Get-ShaderIncludeClosure -Sources @($Source) -IncludeDirectories $IncludeDirectories)
    $content = Get-GeneratedContentFingerprint -Schema 'shader-blob-inputs-v1' -Inputs $closure
    return "$CompilerIdentity|$content|$($closure -join '|')"
}

function Invoke-CachedShaderCompile
{
    param(
        [string] $Compiler, [string] $Source, [string] $Stage, [string] $Entry,
        [string] $Destination, [string[]] $IncludeDirectories, [string] $Output,
        [string] $CacheDirectory, [string] $InputIdentity = '', [string[]] $ExtraArguments = @()
    )
    $compilerInputs = @(Get-ShaderCompilerInputs $Compiler)
    $compilerIdentity = Get-GeneratedContentFingerprint `
        -Schema ('shader-compiler-v1|' + ($compilerInputs -join '|')) -Inputs $compilerInputs
    $currentIdentity = Get-ShaderBlobInputIdentity $Source $IncludeDirectories $compilerIdentity
    if ($InputIdentity -and $InputIdentity -cne $currentIdentity)
    {
        throw "Shader inputs changed since generator capture; retry the build: $Source"
    }
    $InputIdentity = $currentIdentity
    $arguments = @([IO.Path]::GetFullPath($Source), '-s', 'HLSL', '-d', $Destination, '-t', $Stage, '-e', $Entry)
    foreach ($directory in $IncludeDirectories)
    {
        $arguments += @('-I', [IO.Path]::GetFullPath($directory))
    }
    $arguments += $ExtraArguments
    $payload = ConvertTo-Json -Compress -Depth 4 @('shader-blob-v2-ascii-stage', $InputIdentity, $arguments)
    $sha = [Security.Cryptography.SHA256]::Create()
    try
    {
        $key = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($payload)) |
            ForEach-Object { $_.ToString('x2') })
    }
    finally
    {
        $sha.Dispose()
    }
    [IO.Directory]::CreateDirectory($CacheDirectory) | Out-Null
    $blob = Join-Path $CacheDirectory "$key.blob"
    $stamp = Join-Path $CacheDirectory "$key.sha256"
    if ((Test-Path -LiteralPath $blob -PathType Leaf) -and (Test-Path -LiteralPath $stamp -PathType Leaf))
    {
        $expected = [IO.File]::ReadAllText($stamp).Trim()
        if ((Get-Item -LiteralPath $blob).Length -gt 0 -and $expected -ceq (Get-GeneratedContentFileHash $blob))
        {
            Copy-Item -LiteralPath $blob -Destination $Output -Force
            return
        }
    }
    $temporary = Join-Path $CacheDirectory "$key.$([Guid]::NewGuid().ToString('N')).tmp"
    $temporaryStamp = "$temporary.sha256"
    $stageRoot = $null
    try
    {
        $stageBase = [IO.Path]::GetTempPath()
        if ($stageBase -match '[^\x00-\x7F]')
        {
            $stageBase = Join-Path $env:ProgramData 'Keire/ShaderStaging'
        }
        if ($stageBase -match '[^\x00-\x7F]')
        {
            throw 'Shader compiler staging requires an ASCII temporary directory.'
        }
        $stageRoot = [IO.Path]::GetFullPath((Join-Path $stageBase ('KeireShader-' + [Guid]::NewGuid().ToString('N'))))
        [IO.Directory]::CreateDirectory($stageRoot) | Out-Null
        $closure = @(Get-ShaderIncludeClosure @($Source) $IncludeDirectories)
        $commonRoot = Split-Path -Parent ([IO.Path]::GetFullPath($Source))
        foreach ($inputPath in $closure)
        {
            while (-not $inputPath.StartsWith($commonRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase))
            {
                $commonRoot = Split-Path -Parent $commonRoot
                if (-not $commonRoot)
                {
                    throw 'Shader include closure spans unrelated filesystem roots.'
                }
            }
        }
        $stagedPaths = @{}
        foreach ($inputPath in $closure)
        {
            $segments = [IO.Path]::GetRelativePath($commonRoot, $inputPath) -split '[\\/]'
            $safeSegments = foreach ($segment in $segments)
            {
                # Escape every segment, including ASCII, to avoid collisions with encoded names.
                -join ([Text.Encoding]::UTF8.GetBytes($segment) | ForEach-Object { $_.ToString('x2') })
            }
            $stagedPaths[$inputPath] = Join-Path $stageRoot ($safeSegments -join '/')
        }
        foreach ($inputPath in $closure)
        {
            $stagedPath = $stagedPaths[$inputPath]
            [IO.Directory]::CreateDirectory((Split-Path -Parent $stagedPath)) | Out-Null
            $text = [IO.File]::ReadAllText($inputPath)
            $rewritten = [regex]::Replace($text, '(?m)^(\s*#\s*include\s+)"([^"\r\n]+)"', {
                param($match)
                $resolved = $null
                foreach ($directory in @((Split-Path -Parent $inputPath)) + $IncludeDirectories)
                {
                    $candidate = [IO.Path]::GetFullPath((Join-Path $directory $match.Groups[2].Value))
                    if ($stagedPaths.ContainsKey($candidate))
                    {
                        $resolved = $candidate
                        break
                    }
                }
                if (-not $resolved)
                {
                    # A commented directive may not belong to the actual closure.
                    return $match.Value
                }
                $relative = [IO.Path]::GetRelativePath((Split-Path -Parent $stagedPath), $stagedPaths[$resolved]).Replace('\', '/')
                return $match.Groups[1].Value + '"' + $relative + '"'
            })
            [IO.File]::WriteAllText($stagedPath, $rewritten, [Text.UTF8Encoding]::new($false))
        }
        $compileOutput = Join-Path $stageRoot 'compiled.blob'
        $compileArguments = @($stagedPaths[[IO.Path]::GetFullPath($Source)], '-s', 'HLSL', '-d', $Destination,
            '-t', $Stage, '-e', $Entry, '-I', $stageRoot) + $ExtraArguments
        & $Compiler @compileArguments -o $compileOutput
        if ($LASTEXITCODE -ne 0)
        {
            throw "Shader compilation failed: $Source / $Stage / $Entry / $Destination ($LASTEXITCODE)"
        }
        if (-not (Test-Path -LiteralPath $compileOutput -PathType Leaf) -or (Get-Item -LiteralPath $compileOutput).Length -eq 0)
        {
            throw "Shader compiler did not produce a nonempty blob: $Source / $Entry"
        }
        # Never publish a blob under an obsolete dependency identity if inputs changed during compilation.
        $currentCompilerInputs = @(Get-ShaderCompilerInputs $Compiler)
        $currentCompilerIdentity = Get-GeneratedContentFingerprint `
            -Schema ('shader-compiler-v1|' + ($currentCompilerInputs -join '|')) -Inputs $currentCompilerInputs
        $currentIdentity = Get-ShaderBlobInputIdentity $Source $IncludeDirectories $currentCompilerIdentity
        if ($currentIdentity -cne $InputIdentity)
        {
            throw "Shader inputs changed during compilation; retry the build: $Source"
        }
        Copy-Item -LiteralPath $compileOutput -Destination $temporary
        [IO.File]::WriteAllText($temporaryStamp, (Get-GeneratedContentFileHash $temporary))
        Move-Item -LiteralPath $temporary -Destination $blob -Force
        Move-Item -LiteralPath $temporaryStamp -Destination $stamp -Force
        Copy-Item -LiteralPath $blob -Destination $Output -Force
    }
    finally
    {
        Remove-Item -LiteralPath $temporary, $temporaryStamp -Force -ErrorAction SilentlyContinue
        if ($stageRoot)
        {
            $resolvedStage = [IO.Path]::GetFullPath($stageRoot)
            $resolvedBase = [IO.Path]::GetFullPath($stageBase).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
            if ($resolvedStage.StartsWith($resolvedBase, [StringComparison]::OrdinalIgnoreCase) -and
                (Split-Path -Leaf $resolvedStage).StartsWith('KeireShader-'))
            {
                Remove-Item -LiteralPath $resolvedStage -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }
}
