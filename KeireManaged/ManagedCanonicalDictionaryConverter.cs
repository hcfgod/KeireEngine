using System.Text.Json;
using System.Text.Json.Serialization;

namespace Keire;

internal sealed class ManagedCanonicalDictionaryConverterFactory : JsonConverterFactory
{
    public override bool CanConvert(Type typeToConvert) =>
        typeToConvert.IsGenericType && typeToConvert.GetGenericTypeDefinition() == typeof(Dictionary<,>);

    public override JsonConverter CreateConverter(Type typeToConvert, JsonSerializerOptions options)
    {
        Type[] arguments = typeToConvert.GetGenericArguments();
        ValidateKeyType(arguments[0], typeToConvert.FullName ?? typeToConvert.Name);
        return (JsonConverter)Activator.CreateInstance(
            typeof(ManagedCanonicalDictionaryConverter<,>).MakeGenericType(arguments))!;
    }

    internal static void ValidateKeyType(Type type, string path)
    {
        if (type == typeof(string) || type == typeof(bool) || type == typeof(char) || type == typeof(sbyte) ||
            type == typeof(byte) || type == typeof(short) || type == typeof(ushort) || type == typeof(int) ||
            type == typeof(uint) || type == typeof(long) || type == typeof(ulong) || type == typeof(Guid) ||
            type.IsEnum)
        {
            return;
        }
        throw new ManagedSerializationException(
            "KEIRE-MANAGED-SERIALIZATION-0001", $"{path}[key]", type, null,
            "dictionary keys must be strings, booleans, characters, integers, enums, or GUIDs");
    }

}

internal sealed class ManagedCanonicalDictionaryConverter<TKey, TValue> : JsonConverter<Dictionary<TKey, TValue>>
    where TKey : notnull
{
    private const int MaximumEntries = 16_384;

    public override Dictionary<TKey, TValue>? Read(ref Utf8JsonReader reader, Type typeToConvert,
                                                   JsonSerializerOptions options)
    {
        if (reader.TokenType == JsonTokenType.Null)
            return null;
        if (reader.TokenType != JsonTokenType.StartArray)
            throw new JsonException("Managed dictionaries must be encoded as canonical key/value arrays.");

        var result = new Dictionary<TKey, TValue>();
        while (reader.Read() && reader.TokenType != JsonTokenType.EndArray)
        {
            if (result.Count >= MaximumEntries)
                throw new JsonException($"Managed dictionaries cannot exceed {MaximumEntries} entries.");
            if (reader.TokenType != JsonTokenType.StartObject || !reader.Read() ||
                reader.TokenType != JsonTokenType.PropertyName || reader.GetString() != "key" || !reader.Read())
            {
                throw new JsonException("Managed dictionary entries require a key followed by a value.");
            }
            TKey key = JsonSerializer.Deserialize<TKey>(ref reader, options) ??
                       throw new JsonException("Managed dictionary keys cannot be null.");
            if (!reader.Read() || reader.TokenType != JsonTokenType.PropertyName || reader.GetString() != "value" ||
                !reader.Read())
            {
                throw new JsonException("Managed dictionary entries require a value.");
            }
            TValue? value = JsonSerializer.Deserialize<TValue>(ref reader, options);
            if (!reader.Read() || reader.TokenType != JsonTokenType.EndObject)
                throw new JsonException("Managed dictionary entries contain unexpected data.");
            if (!result.TryAdd(key, value!))
                throw new JsonException("Managed dictionaries cannot contain duplicate keys.");
        }
        if (reader.TokenType != JsonTokenType.EndArray)
            throw new JsonException("Managed dictionary data is incomplete.");
        return result;
    }

    public override void Write(Utf8JsonWriter writer, Dictionary<TKey, TValue> value,
                               JsonSerializerOptions options)
    {
        if (!ReferenceEquals(value.Comparer, EqualityComparer<TKey>.Default))
        {
            throw new ManagedSerializationException(
                "KEIRE-MANAGED-SERIALIZATION-0001", typeof(Dictionary<TKey, TValue>).FullName ?? "Dictionary",
                typeof(Dictionary<TKey, TValue>), value.GetType(), "custom dictionary comparers are not supported");
        }
        if (value.Count > MaximumEntries)
            throw new JsonException($"Managed dictionaries cannot exceed {MaximumEntries} entries.");
        writer.WriteStartArray();
        foreach (KeyValuePair<TKey, TValue> entry in value.OrderBy(
                     entry => JsonSerializer.SerializeToElement(entry.Key, options).GetRawText(),
                     StringComparer.Ordinal))
        {
            writer.WriteStartObject();
            writer.WritePropertyName("key");
            JsonSerializer.Serialize(writer, entry.Key, options);
            writer.WritePropertyName("value");
            JsonSerializer.Serialize(writer, entry.Value, options);
            writer.WriteEndObject();
        }
        writer.WriteEndArray();
    }
}
