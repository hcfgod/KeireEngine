using System.Buffers;
using System.Buffers.Text;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using Keire.Distribution;

namespace Keire.Distribution.Publisher;

public sealed partial class SigningKeySource
{
    private const int MaximumEncodedKeyBytes = 32 * 1024;
    private static readonly byte[] s_PrivateKeyHeader = "-----BEGIN PRIVATE KEY-----"u8.ToArray();
    private static readonly byte[] s_PrivateKeyFooter = "-----END PRIVATE KEY-----"u8.ToArray();

    private readonly string? m_FilePath;
    private readonly string? m_EnvironmentVariable;

    private SigningKeySource(string? filePath, string? environmentVariable)
    {
        m_FilePath = filePath;
        m_EnvironmentVariable = environmentVariable;
    }

    public static SigningKeySource FromFile(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException("A private-key file path is required.", nameof(path));
        }

        return new SigningKeySource(Path.GetFullPath(path), null);
    }

    public static SigningKeySource FromEnvironment(string variableName)
    {
        if (string.IsNullOrWhiteSpace(variableName) || !EnvironmentVariableRegex().IsMatch(variableName))
        {
            throw new ArgumentException("The private-key environment variable name is invalid.", nameof(variableName));
        }

        return new SigningKeySource(null, variableName);
    }

    internal LoadedSigningKey Load()
    {
        byte[] encodedKey = m_FilePath is not null ? ReadFile(m_FilePath) : ReadEnvironment(m_EnvironmentVariable!);
        try
        {
            return LoadedSigningKey.Import(encodedKey);
        }
        catch (Exception exception) when (exception is CryptographicException or ArgumentException or FormatException)
        {
            throw new InvalidDataException("The supplied Ed25519 private key is invalid.", exception);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(encodedKey);
        }
    }

    internal static byte[] EncodePem(ReadOnlySpan<byte> privateKey)
    {
        int base64Length = checked(((privateKey.Length + 2) / 3) * 4);
        byte[] base64 = new byte[base64Length];
        byte[]? pem = null;
        bool completed = false;
        try
        {
            OperationStatus status = Base64.EncodeToUtf8(privateKey, base64, out int consumed, out int written);
            if (status != OperationStatus.Done || consumed != privateKey.Length || written != base64.Length)
            {
                throw new CryptographicException("The private key could not be encoded.");
            }

            int lineBreaks = Math.Max(0, (base64Length - 1) / 64);
            pem = new byte[
                s_PrivateKeyHeader.Length + 1 + base64Length + lineBreaks + 1 + s_PrivateKeyFooter.Length + 1];
            int offset = 0;
            s_PrivateKeyHeader.CopyTo(pem, offset);
            offset += s_PrivateKeyHeader.Length;
            pem[offset++] = (byte)'\n';
            for (int index = 0; index < base64.Length; ++index)
            {
                if (index != 0 && index % 64 == 0)
                {
                    pem[offset++] = (byte)'\n';
                }

                pem[offset++] = base64[index];
            }

            pem[offset++] = (byte)'\n';
            s_PrivateKeyFooter.CopyTo(pem, offset);
            offset += s_PrivateKeyFooter.Length;
            pem[offset] = (byte)'\n';
            completed = true;
            return pem;
        }
        finally
        {
            CryptographicOperations.ZeroMemory(base64);
            if (!completed && pem is not null)
            {
                CryptographicOperations.ZeroMemory(pem);
            }
        }
    }

    private static byte[] ReadFile(string path)
    {
        FileInfo file = new(path);
        if (!file.Exists || file.Length is <= 0 or > MaximumEncodedKeyBytes)
        {
            throw new InvalidDataException("The private-key file is missing or outside its size limit.");
        }

        FileSystemSafety.RejectLink(file);
        PrivateKeyPermissions.Validate(file.FullName);
        byte[] pem = File.ReadAllBytes(file.FullName);
        try
        {
            return DecodePem(pem);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(pem);
        }
    }

    private static byte[] ReadEnvironment(string variableName)
    {
        string? value = Environment.GetEnvironmentVariable(variableName, EnvironmentVariableTarget.Process);
        try
        {
            if (string.IsNullOrEmpty(value) || value.Length > MaximumEncodedKeyBytes ||
                value.Any(character => character > 0x7f))
            {
                throw new InvalidDataException(
                    $"Private-key environment variable '{variableName}' is missing or invalid.");
            }

            byte[] base64 = Encoding.ASCII.GetBytes(value);
            try
            {
                return DecodeCanonicalBase64(base64, $"private-key environment variable '{variableName}'");
            }
            finally
            {
                CryptographicOperations.ZeroMemory(base64);
            }
        }
        finally
        {
            Environment.SetEnvironmentVariable(variableName, null, EnvironmentVariableTarget.Process);
        }
    }

    private static byte[] DecodePem(ReadOnlySpan<byte> pem)
    {
        int end = pem.Length;
        while (end > 0 && pem[end - 1] is (byte)'\r' or (byte)'\n')
        {
            --end;
        }

        if (end <= s_PrivateKeyHeader.Length + s_PrivateKeyFooter.Length ||
            !pem[..end].StartsWith(s_PrivateKeyHeader) ||
            !pem[..end].EndsWith(s_PrivateKeyFooter))
        {
            throw new InvalidDataException("The private-key file must contain one PKCS#8 PRIVATE KEY PEM block.");
        }

        int bodyStart = s_PrivateKeyHeader.Length;
        if (bodyStart < end && pem[bodyStart] == (byte)'\r')
        {
            ++bodyStart;
        }

        if (bodyStart >= end || pem[bodyStart++] != (byte)'\n')
        {
            throw new InvalidDataException("The private-key PEM header is malformed.");
        }

        int footerStart = end - s_PrivateKeyFooter.Length;
        int bodyEnd = footerStart;
        if (bodyEnd > bodyStart && pem[bodyEnd - 1] == (byte)'\n')
        {
            --bodyEnd;
            if (bodyEnd > bodyStart && pem[bodyEnd - 1] == (byte)'\r')
            {
                --bodyEnd;
            }
        }
        else
        {
            throw new InvalidDataException("The private-key PEM footer is malformed.");
        }

        byte[] compact = new byte[bodyEnd - bodyStart];
        int compactLength = 0;
        for (int index = bodyStart; index < bodyEnd; ++index)
        {
            byte value = pem[index];
            if (value is (byte)'\r' or (byte)'\n')
            {
                continue;
            }

            compact[compactLength++] = value;
        }

        try
        {
            return DecodeCanonicalBase64(compact.AsSpan(0, compactLength), "private-key PEM");
        }
        finally
        {
            CryptographicOperations.ZeroMemory(compact);
        }
    }

    private static byte[] DecodeCanonicalBase64(ReadOnlySpan<byte> base64, string sourceName)
    {
        if (base64.IsEmpty)
        {
            throw new InvalidDataException($"The {sourceName} is empty.");
        }

        byte[] decodedBuffer = new byte[Base64.GetMaxDecodedFromUtf8Length(base64.Length)];
        OperationStatus status = Base64.DecodeFromUtf8(base64, decodedBuffer, out int consumed, out int written);
        if (status != OperationStatus.Done || consumed != base64.Length)
        {
            CryptographicOperations.ZeroMemory(decodedBuffer);
            throw new InvalidDataException($"The {sourceName} is not valid base64.");
        }

        byte[] encodedCheck = new byte[checked(((written + 2) / 3) * 4)];
        bool canonical;
        try
        {
            status = Base64.EncodeToUtf8(decodedBuffer.AsSpan(0, written), encodedCheck, out consumed, out int encoded);
            canonical = status == OperationStatus.Done && consumed == written && encoded == base64.Length &&
                encodedCheck.AsSpan(0, encoded).SequenceEqual(base64);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(encodedCheck);
        }

        if (!canonical)
        {
            CryptographicOperations.ZeroMemory(decodedBuffer);
            throw new InvalidDataException($"The {sourceName} must use canonical base64 without whitespace.");
        }

        byte[] decoded = new byte[written];
        decodedBuffer.AsSpan(0, written).CopyTo(decoded);
        CryptographicOperations.ZeroMemory(decodedBuffer);
        return decoded;
    }

    [GeneratedRegex("^[A-Za-z_][A-Za-z0-9_]{0,127}$", RegexOptions.CultureInvariant)]
    private static partial Regex EnvironmentVariableRegex();
}
