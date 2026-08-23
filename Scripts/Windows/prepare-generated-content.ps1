[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

foreach ($script in @("build-info.ps1", "builtin-shaders.ps1", "builtin-skinning.ps1", "builtin-vfx.ps1")) {
    & (Join-Path $PSScriptRoot $script)
}
