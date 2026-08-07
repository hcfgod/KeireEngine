using System.Text.Json;
using System.Text.Json.Serialization;

namespace Keire.Distribution;

public static class DistributionJson
{
    public static JsonSerializerOptions Options { get; } = CreateOptions();

    public static T DeserializeStrict<T>(ReadOnlySpan<byte> bytes)
    {
        using JsonDocument document = ParseStrict(bytes.ToArray(), 24);

        return JsonSerializer.Deserialize<T>(bytes, Options)
            ?? throw new InvalidDataException("The JSON document was empty.");
    }

    public static JsonDocument ParseStrict(byte[] bytes, int maximumDepth)
    {
        if (maximumDepth is < 1 or > 256)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumDepth));
        }

        JsonDocument document = JsonDocument.Parse((ReadOnlyMemory<byte>)bytes, new JsonDocumentOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = maximumDepth,
        });
        try
        {
            RejectDuplicateProperties(document.RootElement, "$", 0, maximumDepth);
            return document;
        }
        catch
        {
            document.Dispose();
            throw;
        }
    }

    public static byte[] Serialize<T>(T value)
    {
        byte[] json = JsonSerializer.SerializeToUtf8Bytes(value, Options);
        byte[] terminated = new byte[json.Length + 1];
        json.CopyTo(terminated, 0);
        terminated[^1] = (byte)'\n';
        return terminated;
    }

    private static JsonSerializerOptions CreateOptions()
    {
        return new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            PropertyNameCaseInsensitive = false,
            AllowTrailingCommas = false,
            ReadCommentHandling = JsonCommentHandling.Disallow,
            UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
            WriteIndented = true,
            MaxDepth = 24,
        };
    }

    private static void RejectDuplicateProperties(JsonElement element, string path, int depth, int maximumDepth)
    {
        if (depth > maximumDepth)
        {
            throw new InvalidDataException("The JSON document exceeds the maximum nesting depth.");
        }

        if (element.ValueKind == JsonValueKind.Object)
        {
            HashSet<string> names = new(StringComparer.Ordinal);
            foreach (JsonProperty property in element.EnumerateObject())
            {
                if (!names.Add(property.Name))
                {
                    throw new InvalidDataException($"Duplicate JSON property '{property.Name}' at {path}.");
                }

                RejectDuplicateProperties(property.Value, $"{path}.{property.Name}", depth + 1, maximumDepth);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
        {
            int index = 0;
            foreach (JsonElement item in element.EnumerateArray())
            {
                RejectDuplicateProperties(item, $"{path}[{index}]", depth + 1, maximumDepth);
                ++index;
            }
        }
    }
}
