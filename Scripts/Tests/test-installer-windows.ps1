$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$Windows = Join-Path $Root "Scripts\Windows"

$launcher = Get-Content -LiteralPath (Join-Path $Root "Scripts\project.ps1") -Raw
if (-not ($launcher.Contains('"package-installer"') -and
        $launcher.Contains('Join-Path $WindowsScripts "package-installer.ps1"'))) {
    throw "The Windows launcher does not expose the editor installer command."
}

$packager = Get-Content -LiteralPath (Join-Path $Windows "package-installer.ps1") -Raw
foreach ($contract in @("package-editor.ps1", "Assert-WindowsEditorPackageStage", "makensis.exe", "NSIS.NSIS",
        "KEIRE_WINDOWS_SIGNING_CERT_SHA1", "Get-FileHash")) {
    if (-not $packager.Contains($contract)) { throw "The Windows installer packager is missing '$contract'." }
}

$templatePath = Join-Path $Root "Installer\Windows\KeireEditor.nsi"
$template = Get-Content -LiteralPath $templatePath -Raw
foreach ($contract in @('RequestExecutionLevel user', 'MUI_PAGE_LICENSE', 'MUI_PAGE_COMPONENTS',
        'MUI_PAGE_DIRECTORY', 'Desktop shortcut', 'Start Menu shortcuts', 'WriteUninstaller',
        'INSTALL_MARKER', 'UnsafeUninstall', 'MUI_FINISHPAGE_RUN')) {
    if (-not $template.Contains($contract)) { throw "The NSIS installer is missing '$contract'." }
}
if ($template.IndexOf('INSTALL_MARKER', [StringComparison]::Ordinal) -gt
    $template.IndexOf('RMDir /r "$INSTDIR"', [StringComparison]::Ordinal)) {
    throw "The NSIS uninstaller removes its directory before verifying the installation marker."
}

$makensisCommand = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
$makensisPath = if ($makensisCommand) { $makensisCommand.Source } else { "" }
if (-not $makensisPath) {
    $installedCompiler = Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"
    if (Test-Path -LiteralPath $installedCompiler -PathType Leaf) { $makensisPath = $installedCompiler }
}
if ($makensisPath) {
    $fixture = Join-Path ([IO.Path]::GetTempPath()) ("keire-nsis-test-" + [guid]::NewGuid().ToString("N"))
    try {
        New-Item -ItemType Directory -Force (Join-Path $fixture "source\bin") | Out-Null
        New-Item -ItemType File -Force (Join-Path $fixture "source\bin\Hub.exe") | Out-Null
        $output = Join-Path $fixture "Setup.exe"
        $arguments = @(
            "/DPRODUCT_IDENTIFIER=Fixture",
            "/DPRODUCT_DISPLAY_NAME=Kéire Fixture",
            "/DPRODUCT_VERSION=1.2.3",
            "/DPRODUCT_FILE_VERSION=1.2.3.0",
            "/DPRODUCT_ARCHITECTURE=x86_64",
            "/DHUB_TARGET=Hub",
            "/DSOURCE_DIRECTORY=$(Join-Path $fixture 'source')",
            "/DOUTPUT_PATH=$output",
            "/DLICENSE_PATH=$(Join-Path $Root 'LICENSE.txt')",
            "/DSETUP_ICON_PATH=$(Join-Path $Root 'Config\Branding\Keire.ico')",
            $templatePath
        )
        & $makensisPath @arguments | Out-Null
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output -PathType Leaf)) {
            throw "The NSIS installer template did not compile."
        }
    }
    finally {
        Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Windows installer checks passed."
