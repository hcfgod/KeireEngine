using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace Keire;

public enum JobPriority : byte
{
    Critical,
    High,
    Normal,
    Low,
    Background
}

public enum JobClass : byte
{
    Compute,
    Blocking
}

public enum JobStatus : byte
{
    Waiting,
    Running,
    Succeeded,
    Failed,
    Cancelled
}

public sealed class JobDescription
{
    public string Name { get; init; } = "Managed job";
    public JobPriority Priority { get; init; } = JobPriority.Normal;
    public JobClass Class { get; init; } = JobClass.Compute;
    public IReadOnlyList<Job> Dependencies { get; init; } = Array.Empty<Job>();
}

public sealed class JobContext
{
    internal JobContext(CancellationToken cancellationToken) => CancellationToken = cancellationToken;

    public CancellationToken CancellationToken { get; }
    public bool IsCancellationRequested => CancellationToken.IsCancellationRequested;
}

public sealed class Job : IEquatable<Job>
{
    private readonly ManagedJobState _state;

    internal Job(ManagedJobState state) => _state = state;

    public ulong Id => _state.Id;
    public bool IsValid => true;
    public JobStatus Status => _state.Status;
    public Task Completion => _state.Completion;

    public void Cancel() => _state.Cancel();

    public bool Equals(Job? other) => other is not null && ReferenceEquals(_state, other._state);
    public override bool Equals(object? value) => value is Job other && Equals(other);
    public override int GetHashCode() => _state.GetHashCode();
}

public static unsafe class Jobs
{
    public static Job Submit(Action<JobContext> callback, JobDescription? description = null)
    {
        ArgumentNullException.ThrowIfNull(callback);
        description ??= new JobDescription();
        ArgumentNullException.ThrowIfNull(description.Name);
        ArgumentNullException.ThrowIfNull(description.Dependencies);
        if (description.Dependencies.Count > 1024)
            throw new ArgumentOutOfRangeException(nameof(description), "A managed job accepts at most 1024 dependencies.");

        ulong[] dependencies = CollectDependencyIds(description.Dependencies, out bool cancelledByDependency);
        if (cancelledByDependency)
        {
            var cancelled = new ManagedJobState(callback);
            cancelled.Invoke(3, 0);
            return new Job(cancelled);
        }

        var state = new ManagedJobState(callback);
        GCHandle managedHandle = GCHandle.Alloc(state, GCHandleType.Normal);
        state.SetHandle(GCHandle.ToIntPtr(managedHandle));
        ulong id = NativeRuntime.SubmitManagedJob(
            dependencies, (byte)description.Priority, (byte)description.Class, description.Name,
            GCHandle.ToIntPtr(managedHandle), (IntPtr)(delegate* unmanaged<IntPtr, byte, byte, byte>)&Invoke);
        if (id == 0)
        {
            state.ReleaseHandle();
            throw new InvalidOperationException("The native job scheduler rejected the managed job.");
        }
        state.SetId(id);
        return new Job(state);
    }

    internal static ulong[] CollectDependencyIds(IReadOnlyList<Job> dependencies,
                                                 out bool cancelledByDependency)
    {
        ulong[] result = new ulong[dependencies.Count];
        int retained = 0;
        cancelledByDependency = false;
        for (int index = 0; index < dependencies.Count; ++index)
        {
            Job dependency = dependencies[index] ??
                throw new ArgumentException("Managed job dependencies cannot contain null.", nameof(dependencies));
            JobStatus status = dependency.Status;
            if (status == JobStatus.Succeeded)
                continue;
            if (status is JobStatus.Failed or JobStatus.Cancelled)
            {
                cancelledByDependency = true;
                continue;
            }
            result[retained++] = dependency.Id;
        }
        if (retained != result.Length)
            Array.Resize(ref result, retained);
        return result;
    }

    public static Job Run(Action callback, JobDescription? description = null)
    {
        ArgumentNullException.ThrowIfNull(callback);
        return Submit(_ => callback(), description);
    }

    [UnmanagedCallersOnly]
    private static byte Invoke(IntPtr pointer, byte phase, byte stopRequested)
    {
        ManagedJobState? state = null;
        try
        {
            state = GCHandle.FromIntPtr(pointer).Target as ManagedJobState;
            if (state is null)
                return 1;
            return state.Invoke(phase, stopRequested);
        }
        catch (Exception exception)
        {
            state?.RecordInteropFailure(exception, phase is 1 or 2 or 3);
            return 1;
        }
    }
}

internal sealed class ManagedJobState
{
    private readonly Action<JobContext> _callback;
    private readonly CancellationTokenSource _cancellation = new();
    private readonly TaskCompletionSource _completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private Exception? _failure;
    private IntPtr _handle;
    private long _id;
    private int _status = (int)JobStatus.Waiting;
    private int _released;

    internal ManagedJobState(Action<JobContext> callback) => _callback = callback;

    internal ulong Id => unchecked((ulong)Volatile.Read(ref _id));
    internal JobStatus Status => (JobStatus)Volatile.Read(ref _status);
    internal Task Completion => _completion.Task;
    internal bool IsInteropHandleReleased => Volatile.Read(ref _released) != 0;

    internal void SetHandle(IntPtr handle) => _handle = handle;
    internal void SetId(ulong id) => Volatile.Write(ref _id, unchecked((long)id));

    internal byte Invoke(byte phase, byte stopRequested)
    {
        if (phase == 4)
        {
            _ = RequestCancellationWithoutThrowing();
            return 0;
        }
        if (phase == 0)
        {
            if (stopRequested != 0)
                _ = RequestCancellationWithoutThrowing();
            Volatile.Write(ref _status, (int)JobStatus.Running);
            try
            {
                _callback(new JobContext(_cancellation.Token));
                return 0;
            }
            catch (Exception exception)
            {
                _failure = exception;
                return 1;
            }
        }

        try
        {
            if (phase == 1)
            {
                Volatile.Write(ref _status, (int)JobStatus.Succeeded);
                _completion.TrySetResult();
            }
            else if (phase == 2)
            {
                Volatile.Write(ref _status, (int)JobStatus.Failed);
                _completion.TrySetException(_failure ?? new InvalidOperationException("The native managed job failed."));
            }
            else
            {
                _ = RequestCancellationWithoutThrowing();
                Volatile.Write(ref _status, (int)JobStatus.Cancelled);
                _completion.TrySetCanceled(_cancellation.Token);
            }
            return 0;
        }
        finally
        {
            ReleaseHandle();
        }
    }

    internal void Cancel()
    {
        JobStatus status = Status;
        if (status is JobStatus.Succeeded or JobStatus.Failed or JobStatus.Cancelled)
            return;
        Exception? cancellationFailure = RequestCancellationWithoutThrowing();
        NativeRuntime.CancelManagedJob(Id);
        if (cancellationFailure is not null)
            ExceptionDispatchInfo.Capture(cancellationFailure).Throw();
    }

    internal void RecordInteropFailure(Exception exception, bool terminal)
    {
        _failure = exception;
        if (!terminal)
            return;
        Volatile.Write(ref _status, (int)JobStatus.Failed);
        _completion.TrySetException(exception);
        ReleaseHandle();
    }

    private Exception? RequestCancellationWithoutThrowing()
    {
        if (_cancellation.IsCancellationRequested)
            return null;
        try
        {
            _cancellation.Cancel();
            return null;
        }
        catch (Exception exception)
        {
            return exception;
        }
    }

    internal void ReleaseHandle()
    {
        if (Interlocked.Exchange(ref _released, 1) != 0 || _handle == IntPtr.Zero)
            return;
        GCHandle.FromIntPtr(_handle).Free();
        _handle = IntPtr.Zero;
    }
}
