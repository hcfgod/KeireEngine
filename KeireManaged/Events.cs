using System.Reflection;
using System.Runtime.ExceptionServices;

namespace Keire;

[SerializableType]
public sealed class PersistentEventCall
{
    public bool Enabled = true;
    public Entity Target;
    public ComponentTypeId Component;
    public string Method = string.Empty;

    [NonSerialized] private Type? _cachedTargetType;
    [NonSerialized] private MethodInfo? _cachedMethod;
    [NonSerialized] private ParameterInfo[]? _cachedParameters;
    [NonSerialized] private string _cachedMethodName = string.Empty;

    internal MethodInfo? ResolveMethod(Behaviour target, object?[] arguments)
    {
        Type targetType = target.GetType();
        if (_cachedTargetType == targetType && _cachedMethodName == Method && _cachedMethod is not null &&
            _cachedParameters is not null && ParametersAccept(_cachedParameters, arguments))
        {
            return _cachedMethod;
        }

        _cachedTargetType = targetType;
        _cachedMethodName = Method;
        _cachedMethod = targetType
            .GetMethods(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
            .FirstOrDefault(candidate =>
                string.Equals(candidate.Name, Method, StringComparison.Ordinal) &&
                candidate.ReturnType == typeof(void) &&
                ParametersAccept(candidate.GetParameters(), arguments));
        _cachedParameters = _cachedMethod?.GetParameters();
        return _cachedMethod;
    }

    private static bool ParametersAccept(ParameterInfo[] parameters, object?[] arguments)
    {
        if (parameters.Length != arguments.Length)
            return false;
        for (int index = 0; index < parameters.Length; ++index)
        {
            object? argument = arguments[index];
            Type parameterType = parameters[index].ParameterType;
            if (argument is null)
            {
                if (parameterType.IsValueType && Nullable.GetUnderlyingType(parameterType) is null)
                    return false;
            }
            else if (!parameterType.IsInstanceOfType(argument))
            {
                return false;
            }
        }
        return true;
    }
}

[SerializableType]
public abstract class KeireEventBase
{
    [SerializeField] protected List<PersistentEventCall> persistentCalls = [];

    public int PersistentListenerCount => persistentCalls.Count;

    protected void InvokePersistent(object?[] arguments)
    {
        foreach (PersistentEventCall call in persistentCalls)
        {
            if (!call.Enabled || !call.Target.Id.IsValid || !call.Component.IsValid ||
                string.IsNullOrWhiteSpace(call.Method))
            {
                continue;
            }

            if (!BehaviourRegistry.TryGet(call.Target, call.Component, out Behaviour? target) || target is null)
            {
                Debug.Warn($"Event listener target is unavailable for '{call.Method}'.");
                continue;
            }

            MethodInfo? method = call.ResolveMethod(target, arguments);
            if (method is null)
            {
                Debug.Warn($"Event listener method '{target.GetType().Name}.{call.Method}' is unavailable.");
                continue;
            }

            try
            {
                method.Invoke(target, arguments);
            }
            catch (TargetInvocationException exception) when (exception.InnerException is not null)
            {
                ExceptionDispatchInfo.Capture(exception.InnerException).Throw();
            }
        }
    }

}

[SerializableType]
public sealed class KeireEvent : KeireEventBase
{
    private event Action? RuntimeListeners;

    public void AddListener(Action listener) => RuntimeListeners += listener;
    public void RemoveListener(Action listener) => RuntimeListeners -= listener;
    public void RemoveAllListeners() => RuntimeListeners = null;

    public void Invoke()
    {
        InvokePersistent([]);
        RuntimeListeners?.Invoke();
    }
}

[SerializableType]
public sealed class KeireEvent<T0> : KeireEventBase
{
    private event Action<T0>? RuntimeListeners;

    public void AddListener(Action<T0> listener) => RuntimeListeners += listener;
    public void RemoveListener(Action<T0> listener) => RuntimeListeners -= listener;
    public void RemoveAllListeners() => RuntimeListeners = null;

    public void Invoke(T0 value0)
    {
        InvokePersistent([value0]);
        RuntimeListeners?.Invoke(value0);
    }
}

[SerializableType]
public sealed class KeireEvent<T0, T1> : KeireEventBase
{
    private event Action<T0, T1>? RuntimeListeners;

    public void AddListener(Action<T0, T1> listener) => RuntimeListeners += listener;
    public void RemoveListener(Action<T0, T1> listener) => RuntimeListeners -= listener;
    public void RemoveAllListeners() => RuntimeListeners = null;

    public void Invoke(T0 value0, T1 value1)
    {
        InvokePersistent([value0, value1]);
        RuntimeListeners?.Invoke(value0, value1);
    }
}

[SerializableType]
public sealed class KeireEvent<T0, T1, T2> : KeireEventBase
{
    private event Action<T0, T1, T2>? RuntimeListeners;

    public void AddListener(Action<T0, T1, T2> listener) => RuntimeListeners += listener;
    public void RemoveListener(Action<T0, T1, T2> listener) => RuntimeListeners -= listener;
    public void RemoveAllListeners() => RuntimeListeners = null;

    public void Invoke(T0 value0, T1 value1, T2 value2)
    {
        InvokePersistent([value0, value1, value2]);
        RuntimeListeners?.Invoke(value0, value1, value2);
    }
}

[SerializableType]
public sealed class KeireEvent<T0, T1, T2, T3> : KeireEventBase
{
    private event Action<T0, T1, T2, T3>? RuntimeListeners;

    public void AddListener(Action<T0, T1, T2, T3> listener) => RuntimeListeners += listener;
    public void RemoveListener(Action<T0, T1, T2, T3> listener) => RuntimeListeners -= listener;
    public void RemoveAllListeners() => RuntimeListeners = null;

    public void Invoke(T0 value0, T1 value1, T2 value2, T3 value3)
    {
        InvokePersistent([value0, value1, value2, value3]);
        RuntimeListeners?.Invoke(value0, value1, value2, value3);
    }
}
