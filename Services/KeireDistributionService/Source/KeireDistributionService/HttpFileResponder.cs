using System.Buffers;
using System.Security.Cryptography;
using Keire.Distribution;
using Microsoft.Extensions.Primitives;

namespace Keire.Distribution.Service;

public static class HttpFileResponder
{
    public static async Task SendDocumentAsync(
        HttpContext context,
        DistributionFile file,
        int bufferSize,
        CancellationToken cancellationToken)
    {
        AddSignatureHeaders(context.Response, file.Signature!);
        await SendAsync(
            context,
            file,
            "application/json; charset=utf-8",
            "public, max-age=60, must-revalidate",
            allowRanges: false,
            bufferSize,
            cancellationToken);
    }

    public static Task SendPackageAsync(
        HttpContext context,
        DistributionFile file,
        int bufferSize,
        CancellationToken cancellationToken)
    {
        return SendAsync(
            context,
            file,
            "application/octet-stream",
            "public, max-age=31536000, immutable",
            allowRanges: true,
            bufferSize,
            cancellationToken);
    }

    public static Task SendManifestAsync(
        HttpContext context,
        DistributionFile file,
        int bufferSize,
        CancellationToken cancellationToken)
    {
        return SendAsync(
            context,
            file,
            "application/json; charset=utf-8",
            "public, max-age=31536000, immutable",
            allowRanges: false,
            bufferSize,
            cancellationToken);
    }

    private static async Task SendAsync(
        HttpContext context,
        DistributionFile file,
        string contentType,
        string cacheControl,
        bool allowRanges,
        int bufferSize,
        CancellationToken cancellationToken)
    {
        await using FileStream? stream = await OpenValidatedFileAsync(file, bufferSize, cancellationToken);
        if (stream is null)
        {
            context.Response.Clear();
            await DistributionEndpoints.WriteErrorAsync(
                context,
                StatusCodes.Status503ServiceUnavailable,
                "distribution.snapshot_integrity_failed",
                "The active distribution snapshot failed its immutability check.");
            return;
        }

        string etag = $"\"sha256-{file.Sha256}\"";
        context.Response.Headers.ETag = etag;
        context.Response.Headers.CacheControl = cacheControl;
        context.Response.Headers["X-Content-Type-Options"] = "nosniff";
        if (MatchesIfNoneMatch(context.Request.Headers.IfNoneMatch, etag))
        {
            context.Response.StatusCode = StatusCodes.Status304NotModified;
            return;
        }

        long start = 0;
        long end = file.Size == 0 ? -1 : file.Size - 1;
        bool partial = false;
        if (allowRanges)
        {
            context.Response.Headers.AcceptRanges = "bytes";
            StringValues rangeHeader = context.Request.Headers.Range;
            if (rangeHeader.Count != 0 && IfRangeAllowsRange(context.Request.Headers.IfRange, etag))
            {
                if (!TryParseSingleRange(rangeHeader.ToString(), file.Size, out start, out end))
                {
                    context.Response.StatusCode = StatusCodes.Status416RangeNotSatisfiable;
                    context.Response.Headers.ContentRange = $"bytes */{file.Size}";
                    return;
                }

                partial = true;
            }
        }

        long responseLength = end >= start ? end - start + 1 : 0;
        context.Response.StatusCode = partial ? StatusCodes.Status206PartialContent : StatusCodes.Status200OK;
        context.Response.ContentType = contentType;
        context.Response.ContentLength = responseLength;
        if (partial)
        {
            context.Response.Headers.ContentRange = $"bytes {start}-{end}/{file.Size}";
        }

        if (HttpMethods.IsHead(context.Request.Method) || responseLength == 0)
        {
            return;
        }

        byte[] buffer = ArrayPool<byte>.Shared.Rent(bufferSize);
        try
        {
            stream.Position = start;
            long remaining = responseLength;
            while (remaining > 0)
            {
                int requested = (int)Math.Min(buffer.Length, remaining);
                int count = await stream.ReadAsync(buffer.AsMemory(0, requested), cancellationToken);
                if (count == 0)
                {
                    throw new EndOfStreamException("An immutable distribution file ended unexpectedly.");
                }

                await context.Response.Body.WriteAsync(buffer.AsMemory(0, count), cancellationToken);
                remaining -= count;
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(buffer);
        }
    }

    private static async Task<FileStream?> OpenValidatedFileAsync(
        DistributionFile file,
        int bufferSize,
        CancellationToken cancellationToken)
    {
        try
        {
            FileInfo current = new(file.AbsolutePath);
            if (!current.Exists)
            {
                return null;
            }

            FileSystemSafety.RejectLink(current);
            if (current.Length != file.Size || current.LastWriteTimeUtc != file.LastWriteTimeUtc)
            {
                return null;
            }

            FileStream stream = new(file.AbsolutePath, new FileStreamOptions
            {
                Mode = FileMode.Open,
                Access = FileAccess.Read,
                Share = FileShare.Read,
                BufferSize = bufferSize,
                Options = FileOptions.Asynchronous | FileOptions.SequentialScan,
            });
            try
            {
                if (stream.Length != file.Size)
                {
                    await stream.DisposeAsync();
                    return null;
                }

                byte[] digest = await SHA256.HashDataAsync(stream, cancellationToken);
                if (!string.Equals(Convert.ToHexStringLower(digest), file.Sha256, StringComparison.Ordinal))
                {
                    await stream.DisposeAsync();
                    return null;
                }

                current.Refresh();
                if (!current.Exists || current.Length != file.Size || current.LastWriteTimeUtc != file.LastWriteTimeUtc)
                {
                    await stream.DisposeAsync();
                    return null;
                }

                stream.Position = 0;
                return stream;
            }
            catch
            {
                await stream.DisposeAsync();
                throw;
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }

    private static void AddSignatureHeaders(HttpResponse response, SignedDocumentMetadata signature)
    {
        response.Headers["X-Keire-Signature-Algorithm"] = signature.Algorithm;
        response.Headers["X-Keire-Signature-Key-Id"] = signature.KeyId;
        response.Headers["X-Keire-Signature"] = signature.Value;
        response.Headers["X-Keire-Sequence"] = signature.Sequence.ToString(System.Globalization.CultureInfo.InvariantCulture);
        response.Headers["X-Keire-Expires"] = signature.ExpiresAt.ToString("O", System.Globalization.CultureInfo.InvariantCulture);
    }

    private static bool MatchesIfNoneMatch(StringValues values, string etag)
    {
        foreach (string? header in values)
        {
            if (header is null)
            {
                continue;
            }

            foreach (string token in header.Split(','))
            {
                string candidate = token.Trim();
                if (candidate == "*" || string.Equals(candidate, etag, StringComparison.Ordinal) ||
                    (candidate.StartsWith("W/", StringComparison.Ordinal) &&
                     string.Equals(candidate[2..].TrimStart(), etag, StringComparison.Ordinal)))
                {
                    return true;
                }
            }
        }

        return false;
    }

    private static bool IfRangeAllowsRange(StringValues values, string etag)
    {
        return values.Count == 0 ||
            (values.Count == 1 && string.Equals(values[0]?.Trim(), etag, StringComparison.Ordinal));
    }

    private static bool TryParseSingleRange(string header, long length, out long start, out long end)
    {
        start = 0;
        end = -1;
        if (length <= 0 || !header.StartsWith("bytes=", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        string value = header[6..].Trim();
        if (value.Length == 0 || value.Contains(','))
        {
            return false;
        }

        int separator = value.IndexOf('-');
        if (separator < 0 || value.IndexOf('-', separator + 1) >= 0)
        {
            return false;
        }

        string first = value[..separator].Trim();
        string second = value[(separator + 1)..].Trim();
        if (first.Length == 0)
        {
            if (!TryParseNonNegative(second, out long suffixLength) || suffixLength == 0)
            {
                return false;
            }

            start = Math.Max(0, length - suffixLength);
            end = length - 1;
            return true;
        }

        if (!TryParseNonNegative(first, out start) || start >= length)
        {
            return false;
        }

        if (second.Length == 0)
        {
            end = length - 1;
            return true;
        }

        if (!TryParseNonNegative(second, out long requestedEnd) || requestedEnd < start)
        {
            return false;
        }

        end = Math.Min(requestedEnd, length - 1);
        return true;
    }

    private static bool TryParseNonNegative(string value, out long result)
    {
        return long.TryParse(
            value,
            System.Globalization.NumberStyles.None,
            System.Globalization.CultureInfo.InvariantCulture,
            out result);
    }
}
