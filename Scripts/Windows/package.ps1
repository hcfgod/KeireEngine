[CmdletBinding()]
param([ValidateSet("Release", "Dist")][string]$Configuration = "Release", [string]$Generator = "vs2022", [string]$Architecture = "", [string]$Toolset = "default", [switch]$CI, [switch]$Update, [switch]$Generate)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot; $Project = Get-ProjectConfig; $Lock = Get-DependencyLock
$Architecture = if ($Architecture) { Normalize-Architecture $Architecture } else { Get-NativeArchitecture }
$Toolset = Resolve-WindowsToolset $Generator $Toolset; $outputArchitecture = Get-ArchitectureOutputName $Architecture
& (Join-Path $PSScriptRoot "test.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -CI:$CI -Update:$Update -Generate:$Generate
& (Join-Path $PSScriptRoot "run.ps1") -Generator $Generator -Configuration $Configuration -Architecture $Architecture -Toolset $Toolset -CI:$CI
Enter-WindowsToolEnvironment $Generator $Toolset $Architecture | Out-Null
$name = "$($Project.ARTIFACT_PREFIX)-windows-$Architecture-$Configuration"; $stage = Join-Path $Root "Artifacts\$name"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\bin", "$stage\lib", "$stage\include", "$stage\third-party" | Out-Null
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).exe" "$stage\bin\"
Copy-Item "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CORE_TARGET)\$($Project.CORE_TARGET).lib" "$stage\lib\"
Copy-Item "$Root\$($Project.CORE_DIRECTORY)\Include\*" "$stage\include\" -Recurse
Copy-Item "$Root\Vendor\spdlog\include\spdlog" "$stage\third-party\spdlog\" -Recurse
Copy-Item "$Root\README.md", "$Root\LICENSE.txt", "$Root\THIRD_PARTY_NOTICES.md" $stage
$compiler = if ($Toolset -eq "msc") {
    $majorVersion = if ($Generator -like "vs*") { Get-VisualStudioMajorVersion $Generator } else { 17 }
    "MSVC $((Get-VSBuildEnvironment $majorVersion).InstallationVersion)"
}
elseif ($Toolset -eq "clang") { (& clang --version | Select-Object -First 1) -join "" }
else { (& g++ --version | Select-Object -First 1) -join "" }
$manifest = [ordered]@{ project=$Project.PROJECT_IDENTIFIER; commit=((& git -C $Root rev-parse HEAD 2>$null)-join ""); platform="windows"; architecture=$Architecture; configuration=$Configuration; generator=$Generator; toolset=$Toolset; compiler=$compiler; spdlog=$Lock.SPDLOG_COMMIT; doctest=$Lock.DOCTEST_COMMIT }
$manifest | ConvertTo-Json | Set-Content "$stage\build-manifest.json" -Encoding UTF8
$archive = Join-Path $Root "Artifacts\$name.zip"; Remove-Item $archive -Force -ErrorAction SilentlyContinue; Compress-Archive "$stage\*" $archive
(Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() + "  $name.zip" | Set-Content "$archive.sha256" -Encoding ASCII
$pdb = "$Root\Build\Bin\$Configuration-windows-$outputArchitecture\$($Project.CLIENT_TARGET)\$($Project.CLIENT_TARGET).pdb"
if (Test-Path $pdb) {
    $symbols = Join-Path $Root "Artifacts\$name-symbols.zip"
    Compress-Archive $pdb $symbols -Force
    (Get-FileHash $symbols -Algorithm SHA256).Hash.ToLowerInvariant() + "  $name-symbols.zip" | Set-Content "$symbols.sha256" -Encoding ASCII
}
Write-Host "==> Package created: $archive"
