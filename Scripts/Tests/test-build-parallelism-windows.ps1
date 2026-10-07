$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\Windows\common.ps1')
function Assert-Equal($Actual, $Expected, [string]$Label) {
    if ($Actual -cne $Expected) { throw "$Label expected '$Expected', got '$Actual'." }
}
$previousJobs = $env:KEIRE_BUILD_JOBS
$previousOptions = $env:_CL_
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('keire-build-jobs-' + [guid]::NewGuid().ToString('N'))
try {
    $env:KEIRE_BUILD_JOBS = $null
    Assert-Equal (Get-BuildParallelJobs) ([Math]::Max(1, [Math]::Min(4, [Environment]::ProcessorCount))) 'Default cap'
    foreach ($value in @('1', '2', '17', '2147483647')) {
        $env:KEIRE_BUILD_JOBS = $value
        Assert-Equal (Get-BuildParallelJobs) ([int]$value) 'Explicit job limit'
    }
    foreach ($value in @('0', '-1', '1.5', 'invalid', ' 2', '2 ', '02', '2147483648')) {
        $env:KEIRE_BUILD_JOBS = $value
        $rejected = $false
        try { Get-BuildParallelJobs | Out-Null } catch { $rejected = $_.Exception.Message.Contains('KEIRE_BUILD_JOBS') }
        Assert-Equal $rejected $true "Invalid limit $value"
    }
    $env:KEIRE_BUILD_JOBS = '2'
    $buildPath = Join-Path $PSScriptRoot '..\Windows\build.ps1'
    $tokens = $null; $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($buildPath, [ref]$tokens, [ref]$errors)
    Assert-Equal $errors.Count 0 'Build parser'
    $dispatch = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.SwitchStatementAst] -and $node.Condition.Extent.Text -eq '$Generator'
    }, $true)
    if (-not $dispatch) { throw 'Build generator dispatch was not found.' }
    # Execute the actual dispatch with fake tool processes: no generation, compilation or shared output mutation.
    New-Item -ItemType Directory -Path $fixture | Out-Null
    Set-Content -LiteralPath (Join-Path $fixture 'prepare-generated-content.ps1') -Value ''
    $Root = $fixture; $PSScriptRootForTest = $fixture
    $Target = 'Fixture'; $Configuration = 'Dist'; $Architecture = 'x86_64'
    $WorkspaceName = 'Fixture'; $ProfileBuild = $false; $BuildJobs = Get-BuildParallelJobs
    function Initialize-KeireGeneratedBuild { param($ExpectedFile) }
    function Enter-WindowsToolEnvironment { }
    function Get-VisualStudioMajorVersion { return 17 }
    function Get-MSBuildPlatform { return 'x64' }
    function Get-VSBuildEnvironment { return @{ MSBuild = 'Invoke-FakeBuildTool'; VCTargetsPath = 'fixture' } }
    function Get-NinjaExecutable { return 'Invoke-FakeBuildTool' }
    function Invoke-FakeBuildTool {
        $script:capturedArguments = @($args)
        $script:capturedOptions = $env:_CL_
        $global:LASTEXITCODE = $script:fakeExitCode
    }
    $dispatchText = $dispatch.Extent.Text.Replace('$PSScriptRoot', '$PSScriptRootForTest')
    foreach ($exitCode in @(0, 23)) {
        $script:fakeExitCode = $exitCode
        $Generator = 'vs2022'; $env:_CL_ = '/DKEIRE_FIXTURE'
        $failed = $false
        try { Invoke-Expression $dispatchText } catch { $failed = $_.Exception.Message.Contains('MSBuild failed') }
        Assert-Equal $failed ($exitCode -ne 0) 'MSBuild failure propagation'
        Assert-Equal ($script:capturedArguments -contains '/m:2') $true 'MSBuild worker cap'
        Assert-Equal ($script:capturedArguments -contains '/p:CL_MPCount=1') $true 'MSBuild compiler property'
        Assert-Equal $script:capturedOptions '/DKEIRE_FIXTURE /MP1' 'Raw MP override follows existing options'
        Assert-Equal $env:_CL_ '/DKEIRE_FIXTURE' 'Compiler environment restored after success/failure'
    }
    $script:fakeExitCode = 0; $Generator = 'ninja'
    Invoke-Expression $dispatchText
    Assert-Equal ($script:capturedArguments -join '|') "-j|2|-C|$fixture|-f|build.ninja|Fixture_Dist" 'Ninja bounded invocation'
    $Generator = 'gmake'
    function Get-Command { return @{ Source = 'Invoke-FakeBuildTool' } }
    Invoke-Expression $dispatchText
    Assert-Equal ($script:capturedArguments -join '|') "-j|2|-C|$fixture|config=dist|Fixture" 'Make bounded invocation'
    Remove-Item Function:\Get-Command
    foreach ($name in @('build', 'dependencies', 'coral', 'shader-compiler', 'ffmpeg', 'coverage')) {
        $env:KEIRE_BUILD_JOBS = 'invalid'
        $rejected = $false
        try { & (Join-Path $PSScriptRoot "..\Windows\$name.ps1") } catch {
            $rejected = $_.Exception.Message.Contains('KEIRE_BUILD_JOBS')
        }
        Assert-Equal $rejected $true "$name rejects malformed limits before side effects"
    }
    $env:KEIRE_BUILD_JOBS = '2'
    function cmake { $script:capturedArguments = @($args) }
    foreach ($name in @('dependencies', 'coral', 'shader-compiler')) {
        $sourcePath = Join-Path $PSScriptRoot "..\Windows\$name.ps1"
        $commandAst = [Management.Automation.Language.Parser]::ParseFile($sourcePath, [ref]$tokens, [ref]$errors)
        $buildCommand = $commandAst.Find({ param($node)
            $node -is [Management.Automation.Language.CommandAst] -and
            $node.Extent.Text -like '& cmake --build *--parallel*'
        }, $true)
        if (-not $buildCommand) { throw "$name CMake build command was not found." }
        $build = $fixture; $NativeBuild = $fixture; $cmakeCacheRoot = $fixture
        Invoke-Expression $buildCommand.Extent.Text
        $parallelIndex = [Array]::IndexOf($script:capturedArguments, '--parallel')
        Assert-Equal $script:capturedArguments[$parallelIndex + 1] 2 "$name actual CMake job argument"
    }
    foreach ($name in @('dependencies', 'coral', 'shader-compiler', 'ffmpeg', 'build')) {
        $source = Get-Content -LiteralPath (Join-Path $PSScriptRoot "..\Windows\$name.ps1") -Raw
        Assert-Equal ($source.Contains('$BuildJobs = Get-BuildParallelJobs')) $true "$name validates inherited jobs"
        if ($name -in @('dependencies', 'coral', 'shader-compiler')) {
            Assert-Equal ($source -match '--parallel \$BuildJobs') $true "$name propagates CMake jobs"
        }
    }
    $ffmpeg = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Windows\ffmpeg.ps1') -Raw
    Assert-Equal ($ffmpeg.Contains('$Jobs = $BuildJobs') -and $ffmpeg.Contains('make -j$Jobs')) $true 'FFmpeg bounded invocation'
    Write-Host 'Windows build parallelism tests passed (pure policy and mocked actual generator dispatch).'
}
finally {
    $env:KEIRE_BUILD_JOBS = $previousJobs
    $env:_CL_ = $previousOptions
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    $resolvedFixture = [IO.Path]::GetFullPath($fixture)
    if (-not $resolvedFixture.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $resolvedFixture -Leaf) -notlike 'keire-build-jobs-*') {
        throw 'Refusing to remove an unexpected build-parallelism fixture path.'
    }
    if (Test-Path -LiteralPath $resolvedFixture) { Remove-Item -LiteralPath $resolvedFixture -Recurse -Force }
}
