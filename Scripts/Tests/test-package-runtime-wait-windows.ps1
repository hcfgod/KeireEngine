$ErrorActionPreference = "Stop"
$windows = Join-Path $PSScriptRoot "..\Windows"
. (Join-Path $windows "common.ps1")
$packageSource = Get-Content -LiteralPath (Join-Path $windows "package.ps1") -Raw
# Execute the real smoke block, so reverting to an unawaited GUI invocation breaks this regression.
$begin = $packageSource.IndexOf('$runtimeSmokeResult = Invoke-WindowsExecutableCapture')
$end = $packageSource.IndexOf('$runtimeValidationOutput =', [Math]::Max(0, $begin))
if ($begin -lt 0 -or $end -le $begin) { throw "Packaged runtime smoke must capture and await its child." }
$smoke = [ScriptBlock]::Create($packageSource.Substring($begin, $end - $begin))
$fixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-package-runtime-wait-" + [guid]::NewGuid().ToString("N"))
$previousExit = $env:KEIRE_RUNTIME_WAIT_EXIT
try {
    $stage = Join-Path $fixture "stage"
    $runtimeName = "FixtureRuntime"
    $runtimeExecutionContent = Join-Path $fixture ("content with spaces " + [char]0x00e9)
    New-Item -ItemType Directory -Force (Join-Path $stage "bin"), $runtimeExecutionContent | Out-Null
    Set-Content -LiteralPath (Join-Path $runtimeExecutionContent "retained.txt") -Value "content is live"
    $executable = Join-Path $stage "bin\FixtureRuntime.exe"
    @'
param([string]$Destination)
Add-Type -OutputType WindowsApplication -OutputAssembly $Destination -TypeDefinition @"
using System;
using System.IO;
using System.Threading;
public static class RuntimeWaitFixture
{
    public static int Main(string[] args)
    {
        if (args.Length != 4 || args[0] != "--content" || args[2] != "--frames" || args[3] != "12") return 91;
        Thread.Sleep(350);
        if (!File.Exists(Path.Combine(args[1], "retained.txt"))) return 92;
        File.WriteAllText(Path.Combine(args[1], "completed.txt"), "completed before cleanup");
        Console.WriteLine("runtime GUI fixture completed");
        int code = int.Parse(Environment.GetEnvironmentVariable("KEIRE_RUNTIME_WAIT_EXIT") ?? "0");
        if (code != 0) Console.Error.WriteLine("runtime GUI fixture failure");
        return code;
    }
}
"@
'@ | Set-Content -LiteralPath (Join-Path $fixture "compile.ps1") -Encoding UTF8
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $fixture "compile.ps1") -Destination $executable
    if ($LASTEXITCODE -ne 0) { throw "Could not compile the GUI runtime wait fixture." }
    $env:KEIRE_RUNTIME_WAIT_EXIT = "0"
    & $smoke
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeExecutionContent "completed.txt"))) {
        throw "Runtime smoke returned before its GUI child finished reading content."
    }
    $env:KEIRE_RUNTIME_WAIT_EXIT = "17"
    $failed = $false
    try { & $smoke } catch {
        if ($_.Exception.Message -notmatch 'exit code 17') { throw }
        $failed = $true
    }
    if (-not $failed) { throw "Runtime smoke accepted a failed GUI child." }
    # A missing native exit code must never reuse a previous $LASTEXITCODE value.
    function Invoke-WindowsExecutableCapture {
        param([string]$Path, [string[]]$Arguments, [TimeSpan]$Timeout)
        return [pscustomobject]@{ ExitCode = $null; StandardOutput = ""; StandardError = "" }
    }
    $failed = $false
    try { & $smoke } catch {
        if ($_.Exception.Message -notmatch 'did not report an exit code') { throw }
        $failed = $true
    }
    if (-not $failed) { throw "Runtime smoke accepted an unavailable child exit code." }
    Write-Host "Packaged runtime GUI wait and failure regression passed."
}
finally {
    $env:KEIRE_RUNTIME_WAIT_EXIT = $previousExit
    $resolvedFixture = [IO.Path]::GetFullPath($fixture)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedFixture.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixture) -notlike 'keire-package-runtime-wait-*') {
        throw "Refusing cleanup outside the runtime wait fixture directory."
    }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force -ErrorAction SilentlyContinue
}
