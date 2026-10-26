param(
    [Parameter(Mandatory = $true)]
    [string[]] $Executable
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../Windows/common.ps1')
Enter-WindowsToolEnvironment 'ninja' 'msc' 'x86_64' | Out-Null

foreach ($requestedExecutable in $Executable)
{
    $resolvedExecutable = (Resolve-Path -LiteralPath $requestedExecutable).Path
    $manifestPath = Join-Path ([IO.Path]::GetTempPath()) ('keire-manifest-' + [Guid]::NewGuid().ToString('N') + '.xml')
    try
    {
        & mt.exe -nologo "-inputresource:$resolvedExecutable;#1" "-out:$manifestPath"
        if ($LASTEXITCODE -ne 0)
        {
            throw "Could not extract application manifest from '$resolvedExecutable'."
        }

        [xml] $manifest = Get-Content -LiteralPath $manifestPath -Raw
        $namespaces = [Xml.XmlNamespaceManager]::new($manifest.NameTable)
        $namespaces.AddNamespace('settings', 'http://schemas.microsoft.com/SMI/2016/WindowsSettings')
        $longPathSetting = $manifest.SelectSingleNode('//settings:longPathAware', $namespaces)
        if ($null -eq $longPathSetting -or $longPathSetting.InnerText.Trim() -ne 'true')
        {
            throw "Windows long-path opt-in is missing from '$resolvedExecutable'."
        }

        Write-Host "Verified long-path application manifest: $resolvedExecutable"
    }
    finally
    {
        if (Test-Path -LiteralPath $manifestPath)
        {
            Remove-Item -LiteralPath $manifestPath -Force
        }
    }
}
