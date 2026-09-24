$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\Windows\common.ps1")
$fixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-run-routing-" + [Guid]::NewGuid().ToString("N"))
$previousTrace = $env:KEIRE_RUN_ROUTING_TRACE
try {
    $windows = Join-Path $fixture "Scripts\Windows"
    New-Item -ItemType Directory -Force $windows | Out-Null
    Copy-Item (Join-Path $PSScriptRoot "..\Windows\run.ps1") (Join-Path $windows "run.ps1")
    Copy-Item (Join-Path $PSScriptRoot "..\Windows\run-staged.ps1") (Join-Path $windows "run-staged.ps1")
    @'
function Get-ProjectConfig {
    return [pscustomobject]@{ CLIENT_TARGET = "Client"; HUB_TARGET = "Hub"; PROJECT_NAMESPACE = "Fixture"; ARTIFACT_PREFIX = "fixture" }
}
function Get-RepositoryRoot { return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path }
function Normalize-Architecture([string]$Architecture) { return "x86_64" }
function Get-NativeArchitecture { return "x86_64" }
function Resolve-WindowsToolset { return "msc" }
function Get-ArchitectureOutputName { return "x86_64" }
'@ | Set-Content -LiteralPath (Join-Path $windows "common.ps1") -Encoding UTF8
    @'
param([string]$Target)
[IO.File]::AppendAllText($env:KEIRE_RUN_ROUTING_TRACE, "build $Target`n")
'@ | Set-Content -LiteralPath (Join-Path $windows "build.ps1") -Encoding UTF8
    @'
param([string]$Destination)
Add-Type -OutputType ConsoleApplication -OutputAssembly $Destination -TypeDefinition @"
using System;
using System.IO;
public static class RunRoutingFixture
{
    public static int Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--echo-arguments")
        {
            for (int index = 1; index < args.Length; ++index)
                Console.WriteLine(Convert.ToBase64String(System.Text.Encoding.UTF8.GetBytes(args[index])));
            return 0;
        }
        string name = Path.GetFileNameWithoutExtension(Environment.GetCommandLineArgs()[0]);
        File.AppendAllText(Environment.GetEnvironmentVariable("KEIRE_RUN_ROUTING_TRACE"),
            name + " " + string.Join(" ", args) + "\n");
        if (args.Length == 1 && args[0] == "--invalid")
        {
            Console.Error.WriteLine("Unknown option");
            return 2;
        }
        return 0;
    }
}
"@
'@ | Set-Content -LiteralPath (Join-Path $fixture "compile.ps1") -Encoding UTF8
    $clientDirectory = Join-Path $fixture "Build\Bin\Debug-windows-x86_64\Client"
    $hubDirectory = Join-Path $fixture "Build\Bin\Debug-windows-x86_64\Hub"
    New-Item -ItemType Directory -Force $clientDirectory, $hubDirectory | Out-Null
    $client = Join-Path $clientDirectory "Client.exe"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fixture "compile.ps1") -Destination $client
    if ($LASTEXITCODE -ne 0) { throw "Could not compile the run-routing fixture." }
    Copy-Item -LiteralPath $client -Destination (Join-Path $hubDirectory "Hub.exe")
    $env:KEIRE_RUN_ROUTING_TRACE = Join-Path $fixture "trace.txt"
    $unicodeName = 'Caf' + [char]0x00e9 + ' Project'
    $rawArguments = @('', 'two words', '"quoted"', 'C:\folder with spaces\', '\\"', "tab`tvalue", $unicodeName)
    $echo = Invoke-WindowsExecutableCapture -Path $client -Arguments (@('--echo-arguments') + $rawArguments)
    $expected = ($rawArguments | ForEach-Object {
        [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($_))
    }) -join "`n"
    if ($echo.ExitCode -ne 0 -or $echo.StandardOutput.Replace("`r`n", "`n") -cne ($expected + "`n")) {
        throw "Captured processes did not receive the exact raw argument values."
    }
    $noArguments = Invoke-WindowsExecutableCapture -Path $client
    if ($noArguments.ExitCode -ne 0) { throw "Captured processes must also accept an empty argument list." }
    $cases = @(
        @{ Arguments = @("-SmokeUi"); Target = "Hub" },
        @{ Arguments = @("-SmokeUi", "-Editor"); Target = "Client" },
        @{ Arguments = @("-SmokeUi", "-ProjectPath", (Join-Path $fixture $unicodeName)); Target = "Client" },
        @{ Arguments = @("-SmokeUi", "-SmokeProject"); Target = "" },
        @{ Arguments = @("-SmokeWindow", "-SmokePlay"); Target = "" }
    )
    foreach ($case in $cases) {
        [IO.File]::WriteAllText($env:KEIRE_RUN_ROUTING_TRACE, "")
        $arguments = @("-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
            (Join-Path $windows "run.ps1"), "-Generator", "ninja", "-Toolset", "msc") + $case.Arguments
        $result = Invoke-WindowsExecutableCapture -Path (Get-Command powershell.exe).Source -Arguments $arguments
        $trace = [IO.File]::ReadAllText($env:KEIRE_RUN_ROUTING_TRACE)
        if ($case.Target) {
            if ($result.ExitCode -ne 0 -or $trace -notmatch "(?m)^$($case.Target) --smoke-ui$") {
                throw "UI smoke routing failed for $($case.Arguments -join ' '): $($result.StandardError) $trace"
            }
            if ($case.Target -eq "Client" -and $trace -match '(?m)^build Hub$|^Hub ') {
                throw "Editor UI smoke unexpectedly built or launched the Hub."
            }
        }
        elseif ($result.ExitCode -eq 0 -or $trace) {
            throw "Conflicting smoke modes must fail before building or launching a target."
        }
    }
    $projectPath = Join-Path $fixture $unicodeName
    New-Item -ItemType Directory -Path $projectPath | Out-Null
    foreach ($kind in @("editor", "hub")) {
        $target = if ($kind -eq "editor") { "Client" } else { "Hub" }
        $stageBin = Join-Path $fixture "Build\Distributions\fixture-$kind-windows-x86_64-Dist\bin"
        New-Item -ItemType Directory -Force $stageBin | Out-Null
        Copy-Item -LiteralPath $client -Destination (Join-Path $stageBin "$target.exe")
        [IO.File]::WriteAllText($env:KEIRE_RUN_ROUTING_TRACE, "")
        $arguments = @("-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
            (Join-Path $windows "run-staged.ps1"))
        if ($kind -eq "editor") { $arguments += @("-Editor", "-ProjectPath", $projectPath) }
        $result = Invoke-WindowsExecutableCapture -Path (Get-Command powershell.exe).Source -Arguments $arguments
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $trace = [IO.File]::ReadAllText($env:KEIRE_RUN_ROUTING_TRACE)
            if ($trace) { break }
            Start-Sleep -Milliseconds 20
        } while ([DateTime]::UtcNow -lt $deadline)
        $expectedTrace = if ($kind -eq "editor") { "Client --project $projectPath`n" } else { "Hub `n" }
        if ($result.ExitCode -ne 0 -or $trace -cne $expectedTrace) {
            throw "Staged $kind must launch without building and preserve the project path: $trace $($result.StandardError)"
        }
        Remove-Item -LiteralPath (Join-Path $stageBin "$target.exe")
        $missing = Invoke-WindowsExecutableCapture -Path (Get-Command powershell.exe).Source -Arguments $arguments
        if ($missing.ExitCode -eq 0 -or $missing.StandardError -notmatch "stage-$kind") {
            throw "A missing $kind stage must explain how to create it."
        }
    }
    Write-Host "Windows Hub and Editor smoke and staged-launch routing checks passed."
}
finally {
    $env:KEIRE_RUN_ROUTING_TRACE = $previousTrace
    $resolved = [IO.Path]::GetFullPath($fixture)
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolved.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
}
