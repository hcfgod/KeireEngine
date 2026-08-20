using System.Globalization;
using System.Text;
using System.Text.Json;

namespace Keire;

public static class PlayerPreferences
{
    private const int SchemaVersion = 1;
    private const int MaximumKeyBytes = 256;
    private const int MaximumStringBytes = 64 * 1024;
    private const long MaximumFileBytes = 4L * 1024L * 1024L;
    private const string FileName = "player-preferences.json";
    private static readonly object Gate = new();
    private static readonly SortedDictionary<string, PreferenceEntry> Values = new(StringComparer.Ordinal);
    private static bool s_loaded;
    private static bool s_dirty;
    private static string? s_persistentDataPathOverride;

    public static bool HasKey(string key)
    {
        ValidateKey(key);
        lock (Gate)
        {
            EnsureLoaded();
            return Values.ContainsKey(key);
        }
    }

    public static string GetString(string key, string defaultValue = "")
    {
        ValidateKey(key);
        ArgumentNullException.ThrowIfNull(defaultValue);
        lock (Gate)
        {
            EnsureLoaded();
            return Values.TryGetValue(key, out PreferenceEntry? entry) && entry.Kind == PreferenceKind.String
                ? entry.Value
                : defaultValue;
        }
    }

    public static int GetInt(string key, int defaultValue = 0)
    {
        ValidateKey(key);
        lock (Gate)
        {
            EnsureLoaded();
            return Values.TryGetValue(key, out PreferenceEntry? entry) && entry.Kind == PreferenceKind.Integer &&
                   int.TryParse(entry.Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int value)
                ? value
                : defaultValue;
        }
    }

    public static float GetFloat(string key, float defaultValue = 0.0f)
    {
        ValidateKey(key);
        lock (Gate)
        {
            EnsureLoaded();
            return Values.TryGetValue(key, out PreferenceEntry? entry) && entry.Kind == PreferenceKind.Float &&
                   float.TryParse(entry.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out float value) &&
                   float.IsFinite(value)
                ? value
                : defaultValue;
        }
    }

    public static bool GetBool(string key, bool defaultValue = false)
    {
        ValidateKey(key);
        lock (Gate)
        {
            EnsureLoaded();
            return Values.TryGetValue(key, out PreferenceEntry? entry) && entry.Kind == PreferenceKind.Boolean &&
                   bool.TryParse(entry.Value, out bool value)
                ? value
                : defaultValue;
        }
    }

    public static void SetString(string key, string value)
    {
        ValidateKey(key);
        ArgumentNullException.ThrowIfNull(value);
        if (Encoding.UTF8.GetByteCount(value) > MaximumStringBytes)
            throw new ArgumentOutOfRangeException(nameof(value), "Preference strings cannot exceed 64 KiB of UTF-8.");
        Set(key, PreferenceKind.String, value);
    }

    public static void SetInt(string key, int value) =>
        Set(key, PreferenceKind.Integer, value.ToString(CultureInfo.InvariantCulture));

    public static void SetFloat(string key, float value)
    {
        if (!float.IsFinite(value))
            throw new ArgumentOutOfRangeException(nameof(value), "Preference floats must be finite.");
        Set(key, PreferenceKind.Float, value.ToString("R", CultureInfo.InvariantCulture));
    }

    public static void SetBool(string key, bool value) =>
        Set(key, PreferenceKind.Boolean, value.ToString(CultureInfo.InvariantCulture));

    public static bool DeleteKey(string key)
    {
        ValidateKey(key);
        lock (Gate)
        {
            EnsureLoaded();
            bool removed = Values.Remove(key);
            s_dirty |= removed;
            return removed;
        }
    }

    public static void DeleteAll()
    {
        lock (Gate)
        {
            EnsureLoaded();
            if (Values.Count == 0)
                return;
            Values.Clear();
            s_dirty = true;
        }
    }

    public static void Save()
    {
        lock (Gate)
        {
            EnsureLoaded();
            if (!s_dirty)
                return;
            string path = StoragePath();
            string? directory = Path.GetDirectoryName(path);
            if (string.IsNullOrWhiteSpace(directory))
                throw new InvalidOperationException("The player preference path has no parent directory.");
            Directory.CreateDirectory(directory);
            string temporary = path + $".tmp-{Environment.ProcessId}-{Guid.NewGuid():N}";
            Exception? primaryFailure = null;
            try
            {
                var document = new PreferenceDocument
                {
                    Version = SchemaVersion,
                    Values = new SortedDictionary<string, PreferenceEntry>(Values, StringComparer.Ordinal)
                };
                using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                                                   4096, FileOptions.WriteThrough))
                {
                    JsonSerializer.Serialize(stream, document, JsonOptions);
                    stream.Flush(flushToDisk: true);
                }
                File.Move(temporary, path, overwrite: true);
                s_dirty = false;
            }
            catch (Exception error)
            {
                primaryFailure = error;
                throw;
            }
            finally
            {
                try
                {
                    File.Delete(temporary);
                }
                catch when (primaryFailure is not null)
                {
                }
            }
        }
    }

    internal static void ResetForTests(string? persistentDataPath = null)
    {
        lock (Gate)
        {
            Values.Clear();
            s_loaded = false;
            s_dirty = false;
            s_persistentDataPathOverride = persistentDataPath;
        }
    }

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    private static void Set(string key, PreferenceKind kind, string value)
    {
        ValidateKey(key);
        lock (Gate)
        {
            EnsureLoaded();
            var replacement = new PreferenceEntry { Kind = kind, Value = value };
            if (Values.TryGetValue(key, out PreferenceEntry? current) && current.Kind == kind &&
                current.Value == value)
                return;
            Values[key] = replacement;
            s_dirty = true;
        }
    }

    private static void EnsureLoaded()
    {
        if (s_loaded)
            return;
        string path = StoragePath();
        if (!File.Exists(path))
        {
            s_loaded = true;
            return;
        }
        try
        {
            using FileStream stream = File.OpenRead(path);
            if (stream.Length > MaximumFileBytes)
                throw new InvalidDataException("The player preference file exceeds the 4 MiB safety limit.");
            PreferenceDocument? document = JsonSerializer.Deserialize<PreferenceDocument>(stream, JsonOptions);
            if (document is null || document.Version != SchemaVersion || document.Values is null)
                throw new InvalidDataException("The player preference file has an unsupported schema.");
            var candidate = new SortedDictionary<string, PreferenceEntry>(StringComparer.Ordinal);
            foreach ((string key, PreferenceEntry entry) in document.Values)
            {
                ValidateKey(key);
                ValidateEntry(entry);
                candidate.Add(key, entry);
            }
            Values.Clear();
            foreach ((string key, PreferenceEntry entry) in candidate)
                Values.Add(key, entry);
            s_loaded = true;
        }
        catch (JsonException error)
        {
            throw new InvalidDataException("The player preference file is not valid JSON.", error);
        }
    }

    private static string StoragePath()
    {
        string root = s_persistentDataPathOverride ?? Application.PersistentDataPath;
        if (string.IsNullOrWhiteSpace(root) || !Path.IsPathFullyQualified(root))
            throw new InvalidOperationException("Application.PersistentDataPath must be an absolute path.");
        return Path.Combine(root, FileName);
    }

    private static void ValidateKey(string key)
    {
        ArgumentNullException.ThrowIfNull(key);
        if (string.IsNullOrWhiteSpace(key) || Encoding.UTF8.GetByteCount(key) > MaximumKeyBytes)
            throw new ArgumentException("Preference keys must contain text and cannot exceed 256 UTF-8 bytes.",
                                        nameof(key));
    }

    private static void ValidateEntry(PreferenceEntry? entry)
    {
        if (entry is null || !Enum.IsDefined(entry.Kind) || entry.Value is null ||
            Encoding.UTF8.GetByteCount(entry.Value) > MaximumStringBytes)
            throw new InvalidDataException("The player preference file contains an invalid value.");
        bool valid = entry.Kind switch
        {
            PreferenceKind.String => true,
            PreferenceKind.Integer => int.TryParse(entry.Value, NumberStyles.Integer, CultureInfo.InvariantCulture,
                                                    out _),
            PreferenceKind.Float => float.TryParse(entry.Value, NumberStyles.Float, CultureInfo.InvariantCulture,
                                                   out float value) && float.IsFinite(value),
            PreferenceKind.Boolean => bool.TryParse(entry.Value, out _),
            _ => false
        };
        if (!valid)
            throw new InvalidDataException("The player preference file contains a malformed typed value.");
    }

    private enum PreferenceKind : byte
    {
        String,
        Integer,
        Float,
        Boolean
    }

    private sealed class PreferenceEntry
    {
        public PreferenceKind Kind { get; set; }
        public string Value { get; set; } = string.Empty;
    }

    private sealed class PreferenceDocument
    {
        public int Version { get; set; }
        public SortedDictionary<string, PreferenceEntry>? Values { get; set; }
    }
}
