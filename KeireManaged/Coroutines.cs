using System.Collections;

namespace Keire;

public readonly record struct Coroutine
{
    private readonly CoroutineScheduler? _owner;
    internal Coroutine(CoroutineScheduler owner, ulong id) => (_owner, Id) = (owner, id);
    internal ulong Id { get; }
    public bool IsRunning => _owner?.IsRunning(Id) == true;
    public bool Stop() => _owner?.Stop(Id) == true;
}

public abstract class YieldInstruction
{
    internal abstract bool ShouldKeepWaiting(CoroutinePhase phase, float deltaTime, float unscaledDeltaTime);
}

public abstract class CustomYieldInstruction : YieldInstruction
{
    public abstract bool KeepWaiting { get; }
    internal sealed override bool ShouldKeepWaiting(CoroutinePhase phase, float deltaTime,
                                                    float unscaledDeltaTime) => KeepWaiting;
}

public sealed class WaitForSeconds : YieldInstruction
{
    private float _remaining;
    public WaitForSeconds(float seconds)
    {
        if (!float.IsFinite(seconds) || seconds < 0.0f)
            throw new ArgumentOutOfRangeException(nameof(seconds));
        _remaining = seconds;
    }
    internal override bool ShouldKeepWaiting(CoroutinePhase phase, float deltaTime, float unscaledDeltaTime)
    {
        if (phase == CoroutinePhase.Update)
            _remaining -= deltaTime;
        return _remaining > 0.0f;
    }
}

public sealed class WaitForSecondsRealtime : YieldInstruction
{
    private float _remaining;
    public WaitForSecondsRealtime(float seconds)
    {
        if (!float.IsFinite(seconds) || seconds < 0.0f)
            throw new ArgumentOutOfRangeException(nameof(seconds));
        _remaining = seconds;
    }
    internal override bool ShouldKeepWaiting(CoroutinePhase phase, float deltaTime, float unscaledDeltaTime)
    {
        if (phase == CoroutinePhase.Update)
            _remaining -= unscaledDeltaTime;
        return _remaining > 0.0f;
    }
}

public sealed class WaitForFixedUpdate : YieldInstruction
{
    internal override bool ShouldKeepWaiting(CoroutinePhase phase, float deltaTime,
                                             float unscaledDeltaTime) => phase != CoroutinePhase.FixedUpdate;
}

public sealed class WaitForEndOfFrame : YieldInstruction
{
    internal override bool ShouldKeepWaiting(CoroutinePhase phase, float deltaTime,
                                             float unscaledDeltaTime) => phase != CoroutinePhase.LateUpdate;
}

public sealed class WaitUntil(Func<bool> predicate) : CustomYieldInstruction
{
    private readonly Func<bool> _predicate = predicate ?? throw new ArgumentNullException(nameof(predicate));
    public override bool KeepWaiting => !_predicate();
}

public sealed class WaitWhile(Func<bool> predicate) : CustomYieldInstruction
{
    private readonly Func<bool> _predicate = predicate ?? throw new ArgumentNullException(nameof(predicate));
    public override bool KeepWaiting => _predicate();
}

internal enum CoroutinePhase : byte
{
    FixedUpdate,
    Update,
    LateUpdate
}

internal sealed class CoroutineScheduler
{
    private sealed class State(ulong id, IEnumerator root)
    {
        internal ulong Id { get; } = id;
        internal Stack<IEnumerator> Stack { get; } = new([root]);
        internal object? Waiting { get; set; }
        internal ulong ResumeUpdate { get; set; }
    }

    private readonly Dictionary<ulong, State> _states = [];
    private readonly List<ulong> _iteration = [];
    private ulong _nextId = 1;
    private ulong _update;

    internal Action<Exception>? UnhandledException { get; set; }
    internal int Count => _states.Count;

    internal Coroutine Start(IEnumerator routine)
    {
        ArgumentNullException.ThrowIfNull(routine);
        ulong id = _nextId++;
        if (id == 0)
            id = _nextId++;
        var state = new State(id, routine);
        _states.Add(id, state);
        try
        {
            Advance(state, CoroutinePhase.Update, 0.0f, 0.0f);
        }
        catch (Exception exception)
        {
            _states.Remove(id);
            DisposeAndReport(state, exception);
        }
        return new Coroutine(this, id);
    }

    internal bool IsRunning(ulong id) => id != 0 && _states.ContainsKey(id);
    internal bool Stop(ulong id)
    {
        if (id == 0 || !_states.Remove(id, out State? state))
            return false;
        DisposeAndReport(state);
        return true;
    }

    internal void StopAll()
    {
        State[] states = [.. _states.Values];
        _states.Clear();
        foreach (State state in states)
            DisposeAndReport(state);
    }

    internal void Pump(CoroutinePhase phase, float deltaTime, float unscaledDeltaTime)
    {
        if (phase == CoroutinePhase.Update)
            ++_update;
        _iteration.Clear();
        _iteration.AddRange(_states.Keys);
        foreach (ulong id in _iteration)
        {
            if (!_states.TryGetValue(id, out State? state))
                continue;
            try
            {
                Advance(state, phase, deltaTime, unscaledDeltaTime);
            }
            catch (Exception exception)
            {
                _states.Remove(id);
                DisposeAndReport(state, exception);
            }
        }
    }

    private void Advance(State state, CoroutinePhase phase, float deltaTime, float unscaledDeltaTime)
    {
        if (state.ResumeUpdate > _update || IsWaiting(state.Waiting, phase, deltaTime, unscaledDeltaTime))
            return;
        state.Waiting = null;
        while (state.Stack.Count != 0)
        {
            IEnumerator current = state.Stack.Peek();
            if (!current.MoveNext())
            {
                state.Stack.Pop();
                (current as IDisposable)?.Dispose();
                continue;
            }
            object? yielded = current.Current;
            if (yielded is IEnumerator nested)
            {
                state.Stack.Push(nested);
                continue;
            }
            if (yielded is null)
                state.ResumeUpdate = _update + 1;
            else
                state.Waiting = yielded;
            return;
        }
        _states.Remove(state.Id);
    }

    private static bool IsWaiting(object? waiting, CoroutinePhase phase, float deltaTime, float unscaledDeltaTime)
    {
        return waiting switch
        {
            null => false,
            YieldInstruction instruction =>
                instruction.ShouldKeepWaiting(phase, deltaTime, unscaledDeltaTime),
            Coroutine coroutine => coroutine.IsRunning,
            Task task when !task.IsCompleted => true,
            Task task when task.IsCanceled => throw new TaskCanceledException(task),
            Task task when task.IsFaulted =>
                throw task.Exception?.InnerException ?? task.Exception!,
            Task => false,
            ValueTask valueTask when !valueTask.IsCompleted => true,
            ValueTask valueTask => Complete(valueTask),
            _ => throw new InvalidOperationException(
                $"Coroutine yielded unsupported value '{waiting.GetType().FullName}'.")
        };
    }

    private static bool Complete(ValueTask task)
    {
        task.GetAwaiter().GetResult();
        return false;
    }

    private void DisposeAndReport(State state, Exception? primary = null)
    {
        List<Exception>? failures = primary is null ? null : [primary];
        while (state.Stack.TryPop(out IEnumerator? routine))
        {
            try
            {
                (routine as IDisposable)?.Dispose();
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }
        if (failures is null)
            return;
        Exception failure = failures.Count == 1 ? failures[0] : new AggregateException(failures);
        if (UnhandledException is not null)
            UnhandledException(failure);
        else
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
    }
}
