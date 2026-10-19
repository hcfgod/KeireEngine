Set-StrictMode -Version Latest

function Read-PlatformMigrationBackup
{
    [CmdletBinding()]
    param
    (
        [Parameter(Mandatory)] [string] $Directory,
        [Parameter(Mandatory)] [ValidateSet('database', 'schema')] [string] $Kind,
        [Parameter(Mandatory)] [ValidatePattern('^[a-fA-F0-9]{64}$')] [string] $ExpectedSha256,
        [Parameter(Mandatory)] [ValidateRange(1, 10000)] [int] $ExpectedParts
    )

    $ErrorActionPreference = 'Stop'
    if (-not [IO.Path]::IsPathFullyQualified($Directory))
    {
        throw 'The encrypted backup directory must be an absolute path.'
    }
    $root = Get-Item -LiteralPath $Directory
    if (-not $root.PSIsContainer -or ($root.Attributes -band [IO.FileAttributes]::ReparsePoint))
    {
        throw 'The backup root must be an ordinary directory.'
    }
    $parts = @(Get-ChildItem -LiteralPath $Directory -Filter "$Kind-export-*.dpapi" -File | Sort-Object Name)
    if ($parts.Count -ne $ExpectedParts)
    {
        throw 'The backup part count does not match the trusted manifest.'
    }

    $stream = [IO.MemoryStream]::new()
    $combined = $null
    try
    {
        for ($index = 0; $index -lt $ExpectedParts; $index++)
        {
            $part = $parts[$index]
            $expectedName = '{0}-export-{1:D4}.dpapi' -f $Kind, $index
            if ($part.Name -cne $expectedName -or
                ($part.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
                $part.Length -gt 1048576)
            {
                throw 'The backup contains an unexpected, redirected, or oversized part.'
            }
            $plain = $null
            try
            {
                $plain = [Security.Cryptography.ProtectedData]::Unprotect(
                    [IO.File]::ReadAllBytes($part.FullName),
                    [Text.Encoding]::UTF8.GetBytes('Keire.DatabaseMigration.v1'),
                    [Security.Cryptography.DataProtectionScope]::CurrentUser)
                if ($stream.Length + $plain.Length -gt 268435456)
                {
                    throw 'The decrypted backup exceeds the supported import size.'
                }
                $stream.Write($plain, 0, $plain.Length)
            }
            finally
            {
                if ($null -ne $plain)
                {
                    [Array]::Clear($plain, 0, $plain.Length)
                }
            }
        }

        $combined = $stream.ToArray()
        $actualHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($combined))
        if (-not $actualHash.Equals($ExpectedSha256, [StringComparison]::OrdinalIgnoreCase))
        {
            throw 'The decrypted backup does not match the trusted export digest.'
        }
        # Return text only to the caller; do not log it. Preserve numeric and timestamp representations.
        $strictUtf8 = [Text.UTF8Encoding]::new($false, $true)
        $json = $strictUtf8.GetString($combined)
        $document = [Text.Json.JsonDocument]::Parse($json)
        try
        {
            if ($document.RootElement.ValueKind -ne [Text.Json.JsonValueKind]::Object)
            {
                throw 'The migration backup must contain a JSON object.'
            }
        }
        finally
        {
            $document.Dispose()
        }
        return $json
    }
    finally
    {
        if ($null -ne $combined)
        {
            [Array]::Clear($combined, 0, $combined.Length)
        }
        $buffer = $stream.GetBuffer()
        [Array]::Clear($buffer, 0, $buffer.Length)
        $stream.Dispose()
    }
}

Export-ModuleMember -Function Read-PlatformMigrationBackup
