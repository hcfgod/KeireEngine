[CmdletBinding()]
param
(
    [Parameter(Mandatory)] [string] $SettingsDirectory,
    [Parameter(Mandatory)] [string] $ResultPath,
    [switch] $CreateEmailSchema,
    [switch] $CreateRecoverySchema,
    [switch] $CreateRecoveryDeliverySchema,
    [switch] $CreateRegistrationSchema,
    [switch] $CreatePublicationSequenceSchema,
    [switch] $CreateNativeSchema
)

$ErrorActionPreference = 'Stop'
$settingsBytes = $null
$child = $null
try
{
    foreach ($path in @($SettingsDirectory, $ResultPath))
    {
        if (-not [IO.Path]::IsPathFullyQualified($path))
        {
            throw 'Acceptance paths must be absolute.'
        }
    }
    if (Test-Path -LiteralPath $ResultPath)
    {
        throw 'Use a new result path to preserve previous acceptance evidence.'
    }
    $settingsBytes = [Security.Cryptography.ProtectedData]::Unprotect(
        [IO.File]::ReadAllBytes((Join-Path $SettingsDirectory 'mysql-settings.dpapi')),
        [Text.Encoding]::UTF8.GetBytes('Keire.Platform.MySql.v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Command node.exe).Source)
    $start.WorkingDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../DocumentationSite'))
    $start.ArgumentList.Add('scripts/test-platform-auth-live.mjs')
    if ($CreateEmailSchema)
    {
        $start.ArgumentList.Add('--create-email-schema')
    }
    if ($CreateRecoverySchema)
    {
        $start.ArgumentList.Add('--create-recovery-schema')
    }
    if ($CreateRecoveryDeliverySchema)
    {
        $start.ArgumentList.Add('--create-recovery-delivery-schema')
    }
    if ($CreateRegistrationSchema)
    {
        $start.ArgumentList.Add('--create-registration-schema')
    }
    if ($CreatePublicationSequenceSchema)
    {
        $start.ArgumentList.Add('--create-publication-sequence-schema')
    }
    $start.UseShellExecute = $false
    if ($CreateNativeSchema)
    {
        $start.ArgumentList.Add('--create-native-schema')
    }
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardInputEncoding = [Text.UTF8Encoding]::new($false)
    $child = [Diagnostics.Process]::Start($start)
    $output = $child.StandardOutput.ReadToEndAsync()
    $errors = $child.StandardError.ReadToEndAsync()
    $child.StandardInput.Write([Text.Encoding]::UTF8.GetString($settingsBytes))
    $child.StandardInput.Close()
    if (-not $child.WaitForExit(60000))
    {
        $child.Kill($true)
        throw 'Authentication acceptance timed out; inspect synthetic fixture cleanup before retrying.'
    }
    if ($child.ExitCode -ne 0)
    {
        $diagnostics = $errors.GetAwaiter().GetResult()
        $failureStage = 'unknown'
        $databaseCode = $null
        foreach ($line in ($diagnostics -split "`n"))
        {
            try
            {
                $failure = $line | ConvertFrom-Json
                if ($failure.status -eq 'failed' -and $failure.stage -match '^[a-z-]{1,80}$')
                {
                    $failureStage = $failure.stage
                    if ($failure.databaseCode -cmatch '^ER_[A-Z0-9_]{1,80}$')
                    {
                        $databaseCode = $failure.databaseCode
                    }
                }
            }
            catch
            {
                # Never forward raw driver diagnostics, stack traces, or configuration values.
            }
        }
        @{
            status = 'failed'
            stage = $failureStage
            databaseCode = $databaseCode
            cleanupFailed = $diagnostics.Contains('Synthetic authentication fixture cleanup failed')
        } | ConvertTo-Json | Set-Content -LiteralPath $ResultPath -Encoding utf8
        throw 'Authentication acceptance failed. Inspect the stage report before retrying.'
    }
    $report = $output.GetAwaiter().GetResult()
    $report | ConvertFrom-Json | Out-Null
    $report | Set-Content -LiteralPath $ResultPath -Encoding utf8
    Write-Output $report
}
finally
{
    if ($null -ne $settingsBytes)
    {
        [Array]::Clear($settingsBytes, 0, $settingsBytes.Length)
    }
    if ($null -ne $child)
    {
        $child.Dispose()
    }
}
