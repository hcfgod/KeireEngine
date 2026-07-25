using System.Collections.Concurrent;

namespace Keire;

internal sealed class BehaviourSynchronizationContext : SynchronizationContext
{
    private readonly ConcurrentQueue<(SendOrPostCallback Callback, object? State)> _continuations = new();
    private readonly CancellationTokenSource _lifetime = new();
    private readonly int _simulationThread = Environment.CurrentManagedThreadId;

    public CancellationToken LifetimeToken => _lifetime.Token;
    public bool IsCancelled => _lifetime.IsCancellationRequested;

    public override void Post(SendOrPostCallback callback, object? state)
    {
        ArgumentNullException.ThrowIfNull(callback);
        if (!IsCancelled)
            _continuations.Enqueue((callback, state));
    }

    public override void Send(SendOrPostCallback callback, object? state)
    {
        ArgumentNullException.ThrowIfNull(callback);
        if (Environment.CurrentManagedThreadId != _simulationThread)
            throw new InvalidOperationException("Synchronous managed dispatch must run on the simulation thread.");
        if (!IsCancelled)
            callback(state);
    }

    public void Pump()
    {
        if (Environment.CurrentManagedThreadId != _simulationThread)
            throw new InvalidOperationException("Managed continuations must resume on the simulation thread.");
        for (var count = 0; count < 4096 && !IsCancelled && _continuations.TryDequeue(out var continuation); ++count)
            continuation.Callback(continuation.State);
    }

    public void Cancel()
    {
        if (IsCancelled)
            return;
        _lifetime.Cancel();
        while (_continuations.TryDequeue(out _))
        {
        }
    }
}
