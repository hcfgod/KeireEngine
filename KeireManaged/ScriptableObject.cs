using System.Runtime.ExceptionServices;

namespace Keire;

public abstract class ScriptableObject
{
    private Guid _runtimeInstanceId = Guid.NewGuid();
    private object _lifecycleGate = new();
    private LifecycleState _lifecycleState;

    private enum LifecycleState
    {
        Disabled,
        Enabling,
        Enabled,
        Disabling
    }

    public Guid RuntimeInstanceId => _runtimeInstanceId;
    public string Name { get; set; } = string.Empty;

    protected virtual void OnEnable() { }
    protected virtual void OnDisable() { }
    protected virtual void OnValidate() { }

    public static T CreateInstance<T>() where T : ScriptableObject, new()
    {
        var value = new T();
        bool enabled = false;
        try
        {
            enabled = value.Enable();
            value.OnValidate();
        }
        catch (Exception exception)
        {
            RollBackActivation(value, enabled, exception);
        }
        return value;
    }

    public static T Instantiate<T>(T source) where T : ScriptableObject
    {
        ArgumentNullException.ThrowIfNull(source);
        var clone = ManagedObjectSerializer.Clone(source);
        clone.Enable();
        return clone;
    }

    internal void Validate() => OnValidate();

    internal void RuntimeHydrateManagedData(string document) =>
        ManagedDataHydrator.Restore(this, document);

    internal bool RuntimeRegisterManagedAsset(ulong generation, ulong high, ulong low) =>
        NativeRuntime.RegisterManagedAsset(generation, high, low, this);

    internal bool RuntimeReloadManagedAsset(ulong generation, ulong high, ulong low) =>
        NativeRuntime.ReloadManagedAsset(generation, high, low, this);

    internal bool RuntimeCompleteManagedAssetLoad(ulong generation, ulong high, ulong low) =>
        NativeRuntime.CompleteManagedAssetLoad(generation, high, low, this);

    internal bool Enable()
    {
        lock (_lifecycleGate)
        {
            if (_lifecycleState == LifecycleState.Enabled)
                return false;
            if (_lifecycleState != LifecycleState.Disabled)
                throw new InvalidOperationException("A ScriptableObject lifecycle callback cannot be re-entered.");
            _lifecycleState = LifecycleState.Enabling;
            try
            {
                OnEnable();
                _lifecycleState = LifecycleState.Enabled;
                return true;
            }
            catch
            {
                _lifecycleState = LifecycleState.Disabled;
                throw;
            }
        }
    }

    internal bool Disable()
    {
        lock (_lifecycleGate)
        {
            if (_lifecycleState == LifecycleState.Disabled)
                return false;
            if (_lifecycleState != LifecycleState.Enabled)
                throw new InvalidOperationException("A ScriptableObject lifecycle callback cannot be re-entered.");
            _lifecycleState = LifecycleState.Disabling;
            try
            {
                OnDisable();
                _lifecycleState = LifecycleState.Disabled;
                return true;
            }
            catch
            {
                _lifecycleState = LifecycleState.Enabled;
                throw;
            }
        }
    }

    internal void InitializeCloneRuntimeState()
    {
        _runtimeInstanceId = Guid.NewGuid();
        _lifecycleGate = new object();
        _lifecycleState = LifecycleState.Disabled;
    }

    private static void RollBackActivation(ScriptableObject value, bool enabled, Exception exception)
    {
        if (enabled)
        {
            try
            {
                value.Disable();
            }
            catch (Exception rollbackException)
            {
                throw new AggregateException("ScriptableObject activation rollback failed.", exception,
                                             rollbackException);
            }
        }
        ExceptionDispatchInfo.Capture(exception).Throw();
    }
}
