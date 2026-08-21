using System.Globalization;
using System.Reflection;

namespace Keire;

public enum AssetLoadState : byte
{
    Queued,
    Loading,
    Ready,
    Reloading,
    Failed,
    Cancelled
}

public enum AssetLoadPriority : byte
{
    Critical,
    High,
    Normal,
    Low,
    Background
}

public readonly record struct AssetLoadDiagnostic(string Operation, string Message)
{
    public bool IsEmpty => string.IsNullOrEmpty(Operation) && string.IsNullOrEmpty(Message);
}

public sealed class AssetLoadException : InvalidOperationException
{
    internal AssetLoadException(AssetId asset, AssetLoadDiagnostic diagnostic)
        : base(FormatMessage(asset, diagnostic))
    {
        Asset = asset;
        Diagnostic = diagnostic;
    }

    public AssetId Asset { get; }
    public AssetLoadDiagnostic Diagnostic { get; }

    private static string FormatMessage(AssetId asset, AssetLoadDiagnostic diagnostic)
    {
        string operation = string.IsNullOrWhiteSpace(diagnostic.Operation) ? "load" : diagnostic.Operation;
        string message = string.IsNullOrWhiteSpace(diagnostic.Message) ? "The runtime asset is unavailable."
                                                                       : diagnostic.Message;
        return $"Runtime asset {asset} failed during {operation}: {message}";
    }
}

/// <summary>An explicit native residency lease for a typed runtime asset.</summary>
/// <remarks>
/// The handle never exposes a native object. Dispose it to release the lease; assignment and playback APIs continue to
/// consume the stable <see cref="AssetReference{T}"/> stored in <see cref="Reference"/>.
/// </remarks>
public sealed class AssetHandle<T> : CustomYieldInstruction, IDisposable where T : class
{
    private ulong _handle;

    internal AssetHandle(AssetReference<T> reference, ulong handle)
    {
        Reference = reference;
        _handle = handle;
    }

    public AssetReference<T> Reference { get; }
    public bool IsValid => Volatile.Read(ref _handle) != 0;
    public AssetLoadState State => (AssetLoadState)Status().State;
    public bool IsReady => State is AssetLoadState.Ready or AssetLoadState.Reloading;
    public bool IsDone => State is AssetLoadState.Ready or AssetLoadState.Reloading or AssetLoadState.Failed or
        AssetLoadState.Cancelled;
    public bool UsingFallback => Status().UsingFallback;
    public ulong Revision => Status().Revision;
    public AssetLoadDiagnostic Diagnostic => NativeAssets.GetRuntimeAssetDiagnostic(RequireHandle());
    public override bool KeepWaiting => IsValid && !IsDone;

    public void RequireReady()
    {
        AssetLoadState state = State;
        if (state is AssetLoadState.Ready or AssetLoadState.Reloading)
            return;
        if (state is AssetLoadState.Failed or AssetLoadState.Cancelled)
            throw new AssetLoadException(Reference.Id, Diagnostic);
        throw new InvalidOperationException($"Runtime asset {Reference.Id} is still {state}.");
    }

    public async ValueTask WaitUntilReadyAsync(CancellationToken cancellation = default)
    {
        while (true)
        {
            cancellation.ThrowIfCancellationRequested();
            AssetLoadState state = State;
            if (state is AssetLoadState.Ready or AssetLoadState.Reloading)
                return;
            if (state is AssetLoadState.Failed or AssetLoadState.Cancelled)
                throw new AssetLoadException(Reference.Id, Diagnostic);
            await Task.Yield();
        }
    }

    public void Dispose()
    {
        ulong handle = Interlocked.Exchange(ref _handle, 0);
        if (handle != 0)
            NativeAssets.ReleaseRuntimeAsset(handle);
    }

    private NativeRuntimeAssetStatus Status() => NativeAssets.GetRuntimeAssetStatus(RequireHandle());

    private ulong RequireHandle()
    {
        ulong handle = Volatile.Read(ref _handle);
        return handle != 0 ? handle : throw new ObjectDisposedException(nameof(AssetHandle<T>));
    }
}

internal static class RuntimeAssetType<T> where T : class
{
    private static readonly Lazy<AssetId> s_id = new(Resolve, LazyThreadSafetyMode.ExecutionAndPublication);

    internal static AssetId Id => s_id.Value;

    private static AssetId Resolve()
    {
        Type type = typeof(T);
        if (typeof(ScriptableObject).IsAssignableFrom(type))
            throw new NotSupportedException(
                $"Managed data type '{type.FullName}' uses Assets.LoadAsync instead of a native asset handle.");
        StableAssetTypeIdAttribute? stable = type.GetCustomAttribute<StableAssetTypeIdAttribute>(false);
        if (stable is null)
            throw new NotSupportedException(
                $"Runtime asset marker '{type.FullName}' must declare StableAssetTypeIdAttribute.");
        string compact = stable.Id.ToString("N", CultureInfo.InvariantCulture);
        return new AssetId(ulong.Parse(compact[..16], NumberStyles.HexNumber, CultureInfo.InvariantCulture),
                           ulong.Parse(compact[16..], NumberStyles.HexNumber, CultureInfo.InvariantCulture));
    }
}
