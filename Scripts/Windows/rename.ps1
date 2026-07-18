[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$DisplayName = "",
    [string]$Repository = ""
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$Root = Get-RepositoryRoot; $Project = Get-ProjectConfig
if ($Name -notmatch '^[A-Z][A-Za-z0-9]*$') { throw "Name must be a PascalCase C++ identifier." }
$reserved = @("Alignas","Alignof","And","Asm","Auto","Bool","Break","Case","Catch","Char","Class","Concept","Const","Continue","Default","Delete","Do","Double","Else","Enum","Explicit","Export","Extern","False","Float","For","Friend","Goto","If","Inline","Int","Long","Namespace","New","Noexcept","Not","Nullptr","Operator","Or","Private","Protected","Public","Register","Requires","Return","Short","Signed","Sizeof","Static","Struct","Switch","Template","This","Thread_local","Throw","True","Try","Typedef","Typeid","Typename","Union","Unsigned","Using","Virtual","Void","Volatile","Wchar_t","While")
if ($reserved -contains $Name) { throw "Name '$Name' is a reserved C++ keyword." }
if (-not $DisplayName) { $DisplayName = $Name }
if ($Name -match '[\r\n]' -or $DisplayName -match '[\r\n]' -or $Repository -match '[\r\n]') { throw "Rename values must not contain line breaks." }
if ($Repository -and $Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw "Repository must use owner/name format." }
$newCore = "${Name}Core"; $newClient = "${Name}Client"; $newHub = "${Name}Hub"; $newTests = "${Name}Tests"
$newMacroPrefix = ConvertTo-MacroPrefix $Name
foreach ($path in @($newCore, $newClient, $newHub, $newTests)) {
    if ((Join-Path $Root $path) -notin @((Join-Path $Root $Project.CORE_DIRECTORY), (Join-Path $Root $Project.CLIENT_DIRECTORY), (Join-Path $Root $Project.HUB_DIRECTORY), (Join-Path $Root $Project.TESTS_DIRECTORY)) -and (Test-Path (Join-Path $Root $path))) { throw "Destination '$path' already exists." }
}
# Work directly from the filesystem so clones, copies, archives, nested
# worktrees, and Unicode paths all behave identically.
$rootPrefix = $Root.Path.TrimEnd("\") + "\"
$files = @(Get-ChildItem $Root -File -Recurse | Where-Object {
    $_.FullName -notmatch '[\\/](\.git|Vendor|Tools|Build|Logs|\.vs)[\\/]'
} | ForEach-Object { $_.FullName.Substring($rootPrefix.Length) })
$textExtensions = @(".h", ".hpp", ".cpp", ".c", ".lua", ".ps1", ".sh", ".bat", ".md", ".yml", ".yaml", ".json", ".conf", ".txt", ".gitignore", ".gitattributes", ".editorconfig")
$originals = @{}; $moves = @()
try {
    foreach ($relative in $files) {
        if ($relative -match '^Scripts[\\/](Windows[\\/]rename\.ps1|Unix[\\/]rename\.sh|Tests[\\/])' -or $relative -eq 'Config\PackageConfig.cmake.in') { continue }
        $path = Join-Path $Root $relative
        if (-not (Test-Path $path -PathType Leaf)) { continue }
        if ([IO.Path]::GetExtension($path) -notin $textExtensions -and [IO.Path]::GetFileName($path) -notmatch '^\.(gitignore|gitattributes|editorconfig|clang-format|clang-tidy)$') { continue }
        $content = [IO.File]::ReadAllText($path); $originals[$relative] = $content
        $isCpp = [IO.Path]::GetExtension($path) -in @(".h", ".hpp", ".cpp", ".c")
        $content = $content.Replace("Scripts/Tests", "@@STABLE_TEST_PATH@@").Replace("Scripts\Tests", "@@STABLE_TEST_PATH_WINDOWS@@")
        $content = $content.Replace("Core.log", "@@STABLE_CORE_LOG@@").Replace("Client.log", "@@STABLE_CLIENT_LOG@@")
        $content = $content.Replace($Project.PROJECT_IDENTIFIER, $Name).Replace($Project.PROJECT_DISPLAY_NAME, $DisplayName)
        $content = $content.Replace($Project.PROJECT_MACRO_PREFIX, $newMacroPrefix)
        if ($Repository) { $content = $content.Replace($Project.REPOSITORY_SLUG, $Repository) }
        if ($isCpp) {
            $content = $content.Replace("namespace $($Project.PROJECT_NAMESPACE)", "namespace $Name")
            $content = $content.Replace("$($Project.PROJECT_NAMESPACE)::", "${Name}::")
            $content = $content.Replace('"' + $Project.PROJECT_NAMESPACE + '/', '"' + $Name + '/')
            $content = $content.Replace('"' + $Project.CORE_TARGET + '"', '"' + $newCore + '"')
            $content = $content.Replace('"' + $Project.CLIENT_TARGET + '"', '"' + $newClient + '"')
            $content = $content.Replace('"' + $Project.HUB_TARGET + '"', '"' + $newHub + '"')
        }
        else {
            $content = [regex]::Replace($content, "\b$([regex]::Escape($Project.CORE_TARGET))\b", $newCore)
            $content = [regex]::Replace($content, "\b$([regex]::Escape($Project.CLIENT_TARGET))\b", $newClient)
            $content = [regex]::Replace($content, "\b$([regex]::Escape($Project.HUB_TARGET))\b", $newHub)
            $content = [regex]::Replace($content, "\b$([regex]::Escape($Project.TESTS_TARGET))\b", $newTests)
            $content = $content.Replace("$newCore.h", "Core.h")
            $content = $content.Replace("$newCore::", "${Name}::")
            $content = $content.Replace("${Name}::$newCore", "${Name}::Core")
            $content = $content.Replace("$newCore/Core.h", "$Name/Core.h")
            $content = $content.Replace("$newCore/Log.h", "$Name/Log.h")
        }
        $content = $content.Replace("@@STABLE_TEST_PATH@@", "Scripts/Tests").Replace("@@STABLE_TEST_PATH_WINDOWS@@", "Scripts\Tests")
        $content = $content.Replace("@@STABLE_CORE_LOG@@", "Core.log").Replace("@@STABLE_CLIENT_LOG@@", "Client.log")
        [IO.File]::WriteAllText($path, $content, [Text.UTF8Encoding]::new($false))
    }
    $configPath = Join-Path $Root "Config\Project.conf"
    $config = @("PROJECT_IDENTIFIER=$Name", "PROJECT_DISPLAY_NAME=$DisplayName", "PROJECT_VERSION=$($Project.PROJECT_VERSION)", "PROJECT_NAMESPACE=$Name", "PROJECT_MACRO_PREFIX=$newMacroPrefix", "CORE_TARGET=$newCore", "CORE_DIRECTORY=$newCore", "CLIENT_TARGET=$newClient", "CLIENT_DIRECTORY=$newClient", "HUB_TARGET=$newHub", "HUB_DIRECTORY=$newHub", "TESTS_TARGET=$newTests", "TESTS_DIRECTORY=$newTests", "ARTIFACT_PREFIX=$($Name.ToLowerInvariant())", "REPOSITORY_SLUG=$Repository")
    [IO.File]::WriteAllLines($configPath, $config, [Text.UTF8Encoding]::new($false))
    $publicInclude = Join-Path $Root "$($Project.CORE_DIRECTORY)\Include\$($Project.PROJECT_NAMESPACE)"
    if (Test-Path $publicInclude) { $newInclude = Join-Path (Split-Path $publicInclude) $Name; Move-Item $publicInclude $newInclude; $moves += @($newInclude, $publicInclude) }
    foreach ($pair in @(@($Project.CORE_DIRECTORY,$newCore), @($Project.CLIENT_DIRECTORY,$newClient), @($Project.HUB_DIRECTORY,$newHub), @($Project.TESTS_DIRECTORY,$newTests))) {
        if ($pair[0] -ne $pair[1]) { $from=Join-Path $Root $pair[0]; $to=Join-Path $Root $pair[1]; Move-Item $from $to; $moves += @($to,$from) }
    }
}
catch {
    for ($index=$moves.Count-2; $index -ge 0; $index-=2) { if (Test-Path $moves[$index]) { Move-Item $moves[$index] $moves[$index+1] -Force } }
    foreach ($entry in $originals.GetEnumerator()) { [IO.File]::WriteAllText((Join-Path $Root $entry.Key), $entry.Value, [Text.UTF8Encoding]::new($false)) }
    throw
}
Write-Host "==> Renamed template to $Name. Review the unstaged changes before committing."
