namespace Keire;

public enum InputActionPhase : byte
{
    Disabled,
    Waiting,
    Started,
    Performed,
    Canceled
}

public enum InputValueType : byte
{
    Boolean,
    Axis1D,
    Axis2D
}

internal enum InputContextOperation : byte
{
    EnableAll,
    DisableAll,
    EnableMap,
    DisableMap,
    EnableAction,
    DisableAction
}

public sealed partial class InputActionAsset
{
    private InputActionContext? _sharedContext;
    private ulong _sharedGeneration;

    public InputActionContext CreateContext() => new(this, NativeInput.CreateContext(RequireId()));

    public void Enable() => SharedContext.Enable();
    public void Disable()
    {
        if (_sharedContext is { IsDisposed: false } && _sharedGeneration == NativeRuntime.ManagedAssets?.Generation)
            _sharedContext.Disable();
    }
    public InputActionMap? FindActionMap(string name) => SharedContext.FindActionMap(name);
    public InputAction? FindAction(string path) => SharedContext.FindAction(path);

    private InputActionContext SharedContext
    {
        get
        {
            ulong generation = NativeRuntime.ManagedAssets?.Generation ?? 0;
            if (_sharedContext is null || _sharedContext.IsDisposed || _sharedGeneration != generation)
            {
                _sharedContext?.Dispose();
                _sharedContext = CreateContext();
                _sharedGeneration = generation;
            }
            return _sharedContext;
        }
    }

    private AssetId RequireId() => IsValid ? Id : throw new InvalidOperationException(
        "Input action APIs require a persistent InputActionAsset.");
}

public sealed class InputActionContext : IDisposable
{
    private readonly InputActionAsset _asset;
    private readonly Dictionary<AssetId, InputActionMap> _maps = new();
    private readonly Dictionary<AssetId, InputAction> _actions = new();
    private ulong _handle;

    internal InputActionContext(InputActionAsset asset, ulong handle) => (_asset, _handle) = (asset, handle);

    public InputActionAsset Asset => _asset;
    public bool IsDisposed => _handle == 0;
    internal ulong Handle => _handle != 0 ? _handle : throw new ObjectDisposedException(nameof(InputActionContext));

    public void Enable() => Operate(InputContextOperation.EnableAll);
    public void Disable() => Operate(InputContextOperation.DisableAll);

    public InputActionMap? FindActionMap(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        AssetId id = NativeInput.FindMap(Handle, name);
        return id.IsValid ? GetActionMap(id, name) : null;
    }

    public InputAction? FindAction(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        int separator = path.IndexOf('/');
        if (separator <= 0 || separator == path.Length - 1)
            throw new ArgumentException("Input action paths use the 'Map/Action' form.", nameof(path));
        InputActionMap? map = FindActionMap(path[..separator]);
        return map?.FindAction(path[(separator + 1)..]);
    }

    public InputActionMap GetActionMap(AssetId id, string name = "")
    {
        if (!id.IsValid)
            throw new ArgumentException("An input action map ID must be valid.", nameof(id));
        if (!_maps.TryGetValue(id, out InputActionMap? map))
            _maps.Add(id, map = new InputActionMap(this, id, name));
        return map;
    }

    public InputAction GetAction(AssetId map, AssetId action, string name = "")
    {
        if (!map.IsValid || !action.IsValid)
            throw new ArgumentException("Input action map and action IDs must be valid.");
        if (!_actions.TryGetValue(action, out InputAction? value))
            _actions.Add(action, value = new InputAction(GetActionMap(map), action, name));
        return value;
    }

    internal bool Operate(InputContextOperation operation, AssetId target = default)
    {
        bool changed = NativeInput.OperateContext(Handle, operation, target);
        if (changed && operation is InputContextOperation.DisableAll or InputContextOperation.DisableMap or
            InputContextOperation.DisableAction)
            InputActionRuntime.DispatchContext(this);
        return changed;
    }

    public void Dispose()
    {
        ulong handle = Interlocked.Exchange(ref _handle, 0);
        if (handle == 0)
            return;
        InputActionRuntime.Unregister(this);
        NativeInput.ReleaseContext(handle);
        GC.SuppressFinalize(this);
    }

}

public sealed class InputActionMap
{
    private readonly InputActionContext _context;

    internal InputActionMap(InputActionContext context, AssetId id, string name) =>
        (_context, Id, Name) = (context, id, name);

    public AssetId Id { get; }
    public string Name { get; }
    internal InputActionContext Context => _context;

    public void Enable() => _context.Operate(InputContextOperation.EnableMap, Id);
    public void Disable() => _context.Operate(InputContextOperation.DisableMap, Id);

    public InputAction? FindAction(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        AssetId id = NativeInput.FindAction(_context.Handle, Id, name);
        return id.IsValid ? _context.GetAction(Id, id, name) : null;
    }

    public InputAction GetAction(AssetId id, string name = "") => _context.GetAction(Id, id, name);
}

public sealed class InputAction
{
    public readonly struct CallbackContext
    {
        private readonly NativeInputActionSnapshot _snapshot;
        private readonly InputActionPhase _phase;

        internal CallbackContext(InputAction action, NativeInputActionSnapshot snapshot, InputActionPhase phase) =>
            (Action, _snapshot, _phase) = (action, snapshot, phase);

        public InputAction Action { get; }
        public InputActionPhase Phase => _phase;
        public bool Started => _snapshot.StartedValue != 0;
        public bool Performed => _snapshot.PerformedValue != 0;
        public bool Canceled => _snapshot.CanceledValue != 0;
        public T ReadValue<T>() where T : struct => InputAction.ReadValue<T>(_snapshot);
    }

    private readonly InputActionContext _context;
    private Action<CallbackContext>? _started;
    private Action<CallbackContext>? _performed;
    private Action<CallbackContext>? _canceled;
    private ulong _lastEventFrame = ulong.MaxValue;
    private byte _lastEventMask;

    internal InputAction(InputActionMap map, AssetId id, string name) =>
        (ActionMap, _context, Id, Name) = (map, map.Context, id, name);

    public AssetId Id { get; }
    public string Name { get; }
    public InputActionMap ActionMap { get; }
    public InputActionPhase Phase => (InputActionPhase)Snapshot.Phase;
    public bool Enabled => Snapshot.EnabledValue != 0;
    public bool IsPressed => ReadValueAsButton();
    public bool WasPressedThisFrame => Snapshot.StartedValue != 0;
    public bool WasPerformedThisFrame => Snapshot.PerformedValue != 0;
    public bool WasReleasedThisFrame => Snapshot.CanceledValue != 0;

    public event Action<CallbackContext> started
    {
        add { _started += value; InputActionRuntime.Register(this); }
        remove { _started -= value; }
    }

    public event Action<CallbackContext> performed
    {
        add { _performed += value; InputActionRuntime.Register(this); }
        remove { _performed -= value; }
    }

    public event Action<CallbackContext> canceled
    {
        add { _canceled += value; InputActionRuntime.Register(this); }
        remove { _canceled -= value; }
    }

    public void Enable() => _context.Operate(InputContextOperation.EnableAction, Id);
    public void Disable() => _context.Operate(InputContextOperation.DisableAction, Id);
    public InputRebindOperation BeginInteractiveRebind(AssetId binding) =>
        BeginInteractiveRebind(binding, InputRebindOptions.Default);
    public InputRebindOperation BeginInteractiveRebind(AssetId binding, InputRebindOptions options)
    {
        Input.ValidateRebind(binding, options);
        ulong operation = NativeInput.BeginRebind(_context.Handle, binding, options);
        return operation != 0 ? new InputRebindOperation(operation) : default;
    }
    public bool ReadValueAsButton() => ReadValue<bool>();
    public T ReadValue<T>() where T : struct => ReadValue<T>(Snapshot);

    private NativeInputActionSnapshot Snapshot => NativeInput.ActionSnapshot(_context.Handle, Id);

    internal bool BelongsTo(InputActionContext context) => ReferenceEquals(_context, context);

    internal void Dispatch()
    {
        if (_started is null && _performed is null && _canceled is null)
            return;
        NativeInputActionSnapshot snapshot;
        try { snapshot = Snapshot; }
        catch (InvalidOperationException) { return; }
        byte mask = (byte)((snapshot.StartedValue != 0 ? 1 : 0) | (snapshot.PerformedValue != 0 ? 2 : 0) |
                           (snapshot.CanceledValue != 0 ? 4 : 0));
        if (_lastEventFrame != snapshot.Frame)
        {
            _lastEventFrame = snapshot.Frame;
            _lastEventMask = 0;
        }
        byte pending = (byte)(mask & ~_lastEventMask);
        _lastEventMask |= pending;
        if (pending == 0)
            return;
        if ((pending & 1) != 0)
            Invoke(_started, new CallbackContext(this, snapshot, InputActionPhase.Started));
        if ((pending & 2) != 0)
            Invoke(_performed, new CallbackContext(this, snapshot, InputActionPhase.Performed));
        if ((pending & 4) != 0)
            Invoke(_canceled, new CallbackContext(this, snapshot, InputActionPhase.Canceled));
    }

    private static void Invoke(Action<CallbackContext>? callbacks, CallbackContext context)
    {
        if (callbacks is null)
            return;
        foreach (Action<CallbackContext> callback in callbacks.GetInvocationList().Cast<Action<CallbackContext>>())
        {
            try { callback(context); }
            catch (Exception exception) { Debug.LogException(exception); }
        }
    }

    private static T ReadValue<T>(NativeInputActionSnapshot snapshot) where T : struct
    {
        object value = typeof(T) == typeof(bool) ? snapshot.X >= 0.5f :
            typeof(T) == typeof(float) ? snapshot.X :
            typeof(T) == typeof(Vector2) ? new Vector2(snapshot.X, snapshot.Y) :
            throw new NotSupportedException("Input actions can be read as bool, float, or Vector2.");
        return (T)value;
    }

}

internal static class InputActionRuntime
{
    private static readonly object Sync = new();
    private static readonly List<WeakReference<InputAction>> Actions = new();

    internal static void Register(InputAction action)
    {
        lock (Sync)
        {
            if (Actions.Any(reference => reference.TryGetTarget(out InputAction? value) && ReferenceEquals(value, action)))
                return;
            Actions.Add(new WeakReference<InputAction>(action));
        }
    }

    internal static void Unregister(InputActionContext context)
    {
        lock (Sync)
            Actions.RemoveAll(reference => !reference.TryGetTarget(out InputAction? action) || action.BelongsTo(context));
    }

    internal static void DispatchEvents()
    {
        InputAction[] actions;
        lock (Sync)
        {
            Actions.RemoveAll(reference => !reference.TryGetTarget(out _));
            actions = Actions.Select(reference => reference.TryGetTarget(out InputAction? action) ? action : null)
                .OfType<InputAction>().ToArray();
        }
        foreach (InputAction action in actions)
            action.Dispatch();
    }

    internal static void DispatchContext(InputActionContext context)
    {
        InputAction[] actions;
        lock (Sync)
            actions = Actions.Select(reference => reference.TryGetTarget(out InputAction? action) ? action : null)
                .OfType<InputAction>().Where(action => action.BelongsTo(context)).ToArray();
        foreach (InputAction action in actions)
            action.Dispatch();
    }
}
