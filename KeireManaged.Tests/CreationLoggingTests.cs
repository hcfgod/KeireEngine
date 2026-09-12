using System.Runtime.InteropServices;
using System.Text.Json;
using Keire;

internal static unsafe class CreationLoggingTests
{
    private static string _message = string.Empty;
    private static int _writes;
    private static float _delay;
    private static int _destroys;

    internal static void Run()
    {
        NativeRuntime.WriteLogIcall = &WriteLog;
        NativeRuntime.DestroyEntityDelayedIcall = &DestroyDelayed;
        NativeRuntime.DestroyEntityIcall = &DestroyNow;
        try
        {
            using var catalog = JsonDocument.Parse(ManagedAssetMetadata.Export());
            Check(catalog.RootElement.GetProperty("types").EnumerateArray().Any(type =>
                type.GetProperty("fullName").GetString() == typeof(ConstructorData).FullName),
                "A private default constructor must remain authorable after metadata reload.");
            var data = ScriptableObject.CreateInstance<ConstructorData>();
            Check(data.ModName == "Example" && data.Description == "Default data" && data.IsEnabled,
                  "Constructor defaults must run before activation.");
            data.RuntimeHydrateManagedData("{\"schemaVersion\":1,\"managedTypeId\":\"f3000000-0000-4000-8000-000000000001\",\"fields\":[]}");
            Check(data.ModName == "Example" && data.IsEnabled, "Absent serialized fields retain constructor defaults.");
            Debug.Log("Updated {0}, {1}, {2}", data.ModName, data.Description, data.IsEnabled);
            Check(_message == "Updated Example, Default data, True", "Log must format all arguments in one record.");
            int before = _writes;
            Debug.LogWarning("Object {0}, null [{1}], value {2:F2}", new Vector2(1, 2), null, 1.25f);
            Check(_writes == before + 1 && _message.Contains("null []") && _message.EndsWith("1.25"),
                  "Object null and numeric formatting must produce one invariant log record.");
            Debug.Log(new Vector2(1, 2), new Vector2(3, 4), null);
            Check(_message.Contains("X = 1") && _message.Contains("X = 3") && _message.EndsWith("null"),
                  "Multiple object values must share one log record.");
            Debug.Log("Values", 1, true);
            Check(_message == "Values 1 True", "Unformatted labels must not discard additional values.");
            Debug.Log(null);
            Check(_message == "null", "Single null logs must remain source-compatible.");
            Debug.Log("Literal {0} and {braces}");
            Check(_message == "Literal {0} and {braces}", "Single-string logs must preserve literal braces.");
            Debug.LogError("Escaped {{value}} {0}", 3);
            Check(_message == "Escaped {value} 3", "Escaped braces must format correctly.");
            try { ScriptableObject.CreateInstance<ThrowingConstructorData>(); throw new Exception("Missing constructor failure"); }
            catch (InvalidOperationException error) when (error.Message == "constructor diagnostic") { }
            var entity = new Entity(123, new EntityId(4, 5));
            DestroyCaller.Schedule(entity);
            Check(_delay == 5f && _destroys == 1, "Delayed destruction must forward one request to its owning world.");
            entity.Destroy(0f);
            Check(_delay == 0f && _destroys == 2, "Zero delay preserves immediate destruction.");
            foreach (float invalid in new[] { -1f, float.NaN, float.PositiveInfinity })
            {
                try { entity.Destroy(invalid); throw new Exception("Missing delay rejection"); }
                catch (ArgumentOutOfRangeException) { }
            }
            Check(_destroys == 2, "Invalid delays must leave native state unchanged.");
        }
        finally
        {
            NativeRuntime.WriteLogIcall = null;
            NativeRuntime.DestroyEntityDelayedIcall = null;
            NativeRuntime.DestroyEntityIcall = null;
        }
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    [UnmanagedCallersOnly]
    private static void WriteLog(byte level, NativeString message)
    {
        _message = Marshal.PtrToStringAuto(*(IntPtr*)&message) ?? string.Empty;
        ++_writes;
    }

    [UnmanagedCallersOnly]
    private static byte DestroyDelayed(ulong world, ulong high, ulong low, float delay)
    {
        _delay = delay;
        ++_destroys;
        return world == 123 && high == 4 && low == 5 ? (byte)1 : (byte)0;
    }

    [UnmanagedCallersOnly]
    private static void DestroyNow(ulong world, ulong high, ulong low)
    {
        _delay = 0;
        ++_destroys;
    }
}

internal sealed class DestroyCaller : Behaviour
{
    internal static void Schedule(Entity entity) => Destroy(entity, 5f);
}

[StableAssetTypeId("f3000000-0000-4000-8000-000000000001")]
[CreateAssetMenu("Tests/Constructor Data", "ConstructorData")]
internal sealed class ConstructorData : ScriptableObject
{
    public string ModName;
    public string Description;
    public bool IsEnabled;
    private ConstructorData()
    {
        ModName = "Example";
        Description = "Default data";
        IsEnabled = true;
    }
}

[StableAssetTypeId("f3000000-0000-4000-8000-000000000002")]
internal sealed class ThrowingConstructorData : ScriptableObject
{
    private ThrowingConstructorData() => throw new InvalidOperationException("constructor diagnostic");
}
