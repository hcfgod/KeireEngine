[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][string]$ExtractionRoot,
    [string]$MakensisPath = '',
    [ValidateSet('lzma', 'zlib')][string[]]$Compression = @('lzma', 'zlib')
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source = (Resolve-Path -LiteralPath $SourceDirectory).Path
$destinationParent = [IO.Path]::GetFullPath($ExtractionRoot)
if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    throw 'SourceDirectory must be an existing payload directory.'
}
if (-not $MakensisPath) {
    $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
    $MakensisPath = if ($command) { $command.Source } else {
        Join-Path ${env:ProgramFiles(x86)} 'NSIS/makensis.exe'
    }
}
if (-not (Test-Path -LiteralPath $MakensisPath -PathType Leaf)) {
    throw 'NSIS is required. Supply -MakensisPath or install the normal packaging prerequisite.'
}
$id = [guid]::NewGuid().ToString('N')
$work = Join-Path $repo "Build/Validation/InstallerExtraction/$id"
$sources = @(Get-ChildItem -LiteralPath $source -File -Recurse -Force)
if ($sources.Count -eq 0) { throw 'The benchmark source is empty.' }
$expected = @{}
foreach ($file in $sources) {
    $relative = [IO.Path]::GetRelativePath($source, $file.FullName)
    # Keep the benchmark independent of the machine's optional Win32 long-path policy.
    $longestDestination = Join-Path (Join-Path $destinationParent "lzma-$id") $relative
    if ($longestDestination.Length -ge 260) {
        throw 'Choose a shorter ExtractionRoot; this payload would exceed the benchmark MAX_PATH limit.'
    }
    $expected[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
}
New-Item -ItemType Directory -Path $work -Force | Out-Null
$template = @'
Unicode True
Name "Keire extraction benchmark"
OutFile "${OUTPUT}"
RequestExecutionLevel user
SilentInstall silent
SetCompressor ${COMPRESSION}
Section
SetDetailsPrint none
SetOutPath "$INSTDIR"
File /r "${SOURCE}\*"
IfErrors 0 +2
SetErrorLevel 1
SectionEnd
'@
$templatePath = Join-Path $work 'extract.nsi'
[IO.File]::WriteAllText($templatePath, $template)
$results = @()
foreach ($method in ($Compression | Select-Object -Unique)) {
    $target = Join-Path $destinationParent "$method-$id"
    if (Test-Path -LiteralPath $target) { throw "Benchmark output already exists: $target" }
    $executable = Join-Path $work "$method.exe"
    $buildTimer = [Diagnostics.Stopwatch]::StartNew()
    & $MakensisPath /V2 /WX "/DOUTPUT=$executable" "/DSOURCE=$source" "/DCOMPRESSION=$method" $templatePath
    if ($LASTEXITCODE -ne 0) { throw "NSIS benchmark packaging failed: $method" }
    $buildTimer.Stop()
    $extractTimer = [Diagnostics.Stopwatch]::StartNew()
    # NSIS requires /D last and its directory value unquoted, including when it contains spaces.
    $process = Start-Process -FilePath $executable -ArgumentList "/S /D=$target" -WindowStyle Hidden -Wait -PassThru
    $extractTimer.Stop()
    if ($process.ExitCode -ne 0) { throw "NSIS benchmark extraction failed: $method" }
    $files = @(Get-ChildItem -LiteralPath $target -File -Recurse -Force)
    if ($files.Count -ne $expected.Count) { throw 'The extracted inventory does not match the source.' }
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($target, $file.FullName)
        if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $expected[$relative]) {
            throw "Extracted file hash mismatch: $relative"
        }
    }
    $results += [ordered]@{
        compression = $method
        sizeBytes = (Get-Item -LiteralPath $executable).Length
        buildSeconds = $buildTimer.Elapsed.TotalSeconds
        extractionSeconds = $extractTimer.Elapsed.TotalSeconds
        verifiedFileCount = $files.Count
        destination = $target
    }
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $work 'results.json') -Encoding utf8
}
Write-Host "Verified extraction measurements: $work/results.json"
Write-Host 'Benchmark files are retained for inspection; installed products and registration were not changed.'
