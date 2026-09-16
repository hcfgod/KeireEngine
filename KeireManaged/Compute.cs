using System.Runtime.InteropServices;

namespace Keire;

public enum ComputeBackend : byte
{
    D3D12,
    Vulkan,
    Metal
}

/// <summary>An independent compute device. All operations and disposal belong to its creation thread.</summary>
public sealed class ComputeDevice : IDisposable
{
    private readonly int _thread = Environment.CurrentManagedThreadId;
    private ulong _id;

    public ComputeDevice(ComputeBackend backend)
    {
        if (!Enum.IsDefined(backend))
            throw new ArgumentOutOfRangeException(nameof(backend));
        _id = NativeCompute.Call(0, ComputeCommand.CreateDevice, (ulong)backend);
    }

    internal ulong Id
    {
        get
        {
            RequireThread();
            ObjectDisposedException.ThrowIf(_id == 0, this);
            return _id;
        }
    }

    internal bool IsDisposed => _id == 0;

    internal void RequireThread()
    {
        if (Environment.CurrentManagedThreadId != _thread)
            throw new InvalidOperationException("Compute operations must run on the device creation thread.");
    }

    public ComputeBuffer CreateBuffer(uint size, bool indirect = false)
    {
        if (size == 0 || size % 4 != 0 || size > 128U * 1024U * 1024U || (indirect && size < 12))
            throw new ArgumentOutOfRangeException(nameof(size));
        return new ComputeBuffer(this, NativeCompute.Call(Id, ComputeCommand.CreateBuffer, size, indirect ? 1UL : 0),
                                 size, indirect);
    }

    /// <summary>Uses the opaque key returned by the host's ScriptSystem.RegisterComputeProgram.</summary>
    public ComputePipeline CreatePipeline(ulong programKey, uint variant = 0)
    {
        if (programKey == 0)
            throw new ArgumentOutOfRangeException(nameof(programKey));
        return new ComputePipeline(this, NativeCompute.Call(Id, ComputeCommand.CreatePipeline, programKey, variant));
    }

    public ComputeSubmission Dispatch(ComputePipeline pipeline, ReadOnlySpan<ComputeBufferBinding> bindings,
                                      uint x, uint y = 1, uint z = 1, ReadOnlySpan<byte> uniforms = default)
    {
        if (x == 0 || y == 0 || z == 0 || x > 65535 || y > 65535 || z > 65535)
            throw new ArgumentOutOfRangeException(nameof(x), "Dispatch dimensions must be in 1..65535.");
        return Submit(pipeline, bindings, x, y, z, null, 0, uniforms);
    }

    public ComputeSubmission DispatchIndirect(ComputePipeline pipeline, ReadOnlySpan<ComputeBufferBinding> bindings,
                                              ComputeBuffer arguments, uint offset = 0,
                                              ReadOnlySpan<byte> uniforms = default)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        arguments.Require(this);
        if (!arguments.Indirect || offset % 4 != 0 || offset > arguments.Size || arguments.Size - offset < 12)
            throw new ArgumentException("Indirect arguments require an aligned range of three uint values.", nameof(arguments));
        return Submit(pipeline, bindings, 0, 0, 0, arguments, offset, uniforms);
    }

    private ComputeSubmission Submit(ComputePipeline pipeline, ReadOnlySpan<ComputeBufferBinding> bindings,
                                      uint x, uint y, uint z, ComputeBuffer? arguments, uint offset,
                                      ReadOnlySpan<byte> uniforms)
    {
        ArgumentNullException.ThrowIfNull(pipeline);
        pipeline.Require(this);
        if (bindings.Length > 1024)
            throw new ArgumentOutOfRangeException(nameof(bindings));
        var native = new NativeComputeBinding[bindings.Length];
        var slots = new HashSet<(uint, bool)>();
        for (int index = 0; index < bindings.Length; ++index)
        {
            ComputeBufferBinding binding = bindings[index];
            ArgumentNullException.ThrowIfNull(binding.Buffer);
            binding.Buffer.Require(this);
            if (!slots.Add((binding.Slot, binding.Writable)))
                throw new ArgumentException("Compute binding slots must be unique within each access class.", nameof(bindings));
            native[index] = new NativeComputeBinding { Buffer = binding.Buffer.Handle, Slot = binding.Slot,
                                                       Writable = binding.Writable ? 1U : 0U };
        }
        return new ComputeSubmission(this, NativeCompute.Submit(Id, pipeline.Handle, native, x, y, z,
                                                                arguments?.Handle ?? 0, offset, uniforms));
    }

    public void Dispose()
    {
        if (_id == 0)
            return;
        RequireThread();
        NativeCompute.Call(_id, ComputeCommand.DestroyDevice);
        _id = 0;
    }
}

public readonly record struct ComputeBufferBinding(uint Slot, ComputeBuffer Buffer, bool Writable = false);

/// <summary>Device-owned storage. Native release preserves pending GPU users.</summary>
public sealed class ComputeBuffer : IDisposable
{
    private readonly ComputeDevice _device;
    internal ulong Handle { get; private set; }
    public uint Size { get; }
    public bool Indirect { get; }
    internal ComputeBuffer(ComputeDevice device, ulong handle, uint size, bool indirect)
    { _device = device; Handle = handle; Size = size; Indirect = indirect; }

    internal void Require(ComputeDevice device)
    {
        _ = _device.Id;
        ObjectDisposedException.ThrowIf(Handle == 0, this);
        if (!ReferenceEquals(_device, device))
            throw new ArgumentException("Compute resources belong to a different device.");
    }

    public void Upload(ReadOnlySpan<byte> bytes, uint offset = 0)
    {
        Require(_device);
        if (bytes.Length == 0 || offset % 4 != 0 || bytes.Length % 4 != 0 || offset > Size || (ulong)bytes.Length > Size - offset)
            throw new ArgumentOutOfRangeException(nameof(offset));
        NativeCompute.Upload(_device.Id, Handle, offset, bytes);
    }

    public ComputeSubmission RequestReadback(uint offset = 0, uint size = 0)
    {
        Require(_device);
        if (size == 0 && offset <= Size)
            size = Size - offset;
        if (size == 0 || offset % 4 != 0 || size % 4 != 0 || offset > Size || size > Size - offset)
            throw new ArgumentOutOfRangeException(nameof(size));
        return new ComputeSubmission(_device,
            NativeCompute.WithUInt(_device.Id, ComputeCommand.RequestReadback, Handle, offset, size), size);
    }

    public byte[] Readback(uint offset = 0, uint size = 0)
    {
        Require(_device);
        if (offset > Size || size > Size - offset)
            throw new ArgumentOutOfRangeException(nameof(offset));
        var bytes = new byte[checked((int)(size == 0 ? Size - offset : size))];
        if (bytes.Length != 0)
            NativeCompute.Readback(_device.Id, Handle, offset, bytes);
        return bytes;
    }

    public void Dispose()
    {
        if (Handle == 0)
            return;
        _device.RequireThread();
        if (!_device.IsDisposed)
            NativeCompute.Call(_device.Id, ComputeCommand.DestroyBuffer, Handle);
        Handle = 0;
    }
}

public sealed class ComputePipeline : IDisposable
{
    private readonly ComputeDevice _device;
    internal ulong Handle { get; private set; }
    internal ComputePipeline(ComputeDevice device, ulong handle) { _device = device; Handle = handle; }
    public void Reload(ulong programKey, uint variant = 0)
    {
        Require(_device);
        if (programKey == 0)
            throw new ArgumentOutOfRangeException(nameof(programKey));
        NativeCompute.WithUInt(_device.Id, ComputeCommand.ReloadPipeline, Handle, programKey, variant);
    }

    internal void Require(ComputeDevice device)
    {
        _ = _device.Id;
        ObjectDisposedException.ThrowIf(Handle == 0, this);
        if (!ReferenceEquals(_device, device))
            throw new ArgumentException("Compute resources belong to a different device.");
    }
    public void Dispose()
    {
        if (Handle == 0)
            return;
        _device.RequireThread();
        if (!_device.IsDisposed)
            NativeCompute.Call(_device.Id, ComputeCommand.DestroyPipeline, Handle);
        Handle = 0;
    }
}

public sealed class ComputeSubmission : IDisposable
{
    private readonly ComputeDevice _device;
    private ulong _handle;
    private readonly uint _readbackSize;
    internal ComputeSubmission(ComputeDevice device, ulong handle, uint readbackSize = 0)
    { _device = device; _handle = handle; _readbackSize = readbackSize; }
    /// <summary>Waits for the snapshot; repeated retrieval is valid until disposal.</summary>
    public byte[] GetReadback()
    {
        ulong device = DeviceId;
        if (_readbackSize == 0)
            throw new InvalidOperationException("The submission is not a readback request.");
        var bytes = new byte[checked((int)_readbackSize)];
        NativeCompute.GetReadback(device, _handle, bytes);
        return bytes;
    }
    private ulong DeviceId
    {
        get { ObjectDisposedException.ThrowIf(_handle == 0, this); return _device.Id; }
    }
    public bool IsComplete => NativeCompute.Call(DeviceId, ComputeCommand.IsComplete, _handle) != 0;
    public void Wait() => NativeCompute.Call(DeviceId, ComputeCommand.Wait, _handle);
    public void Dispose()
    {
        if (_handle == 0)
            return;
        _device.RequireThread();
        if (!_device.IsDisposed)
            NativeCompute.Call(_device.Id, ComputeCommand.ReleaseSubmission, _handle);
        _handle = 0;
    }
}

internal enum ComputeCommand : byte
{
    CreateDevice, DestroyDevice, CreateBuffer, DestroyBuffer, CreatePipeline, DestroyPipeline,
    Upload, Readback, IsComplete, Wait, ReleaseSubmission, ReloadPipeline, RequestReadback, GetReadback
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeComputeBinding
{
    internal ulong Buffer;
    internal uint Slot;
    internal uint Writable;
}

internal static unsafe class NativeCompute
{
#pragma warning disable CS0649
    internal static delegate* unmanaged<ulong, byte, ulong, ulong, byte*, uint, ulong*, byte> CommandIcall;
    internal static delegate* unmanaged<ulong, ulong, NativeComputeBinding*, uint, uint, uint, uint, ulong, uint,
                                        byte*, uint, ulong*, byte> DispatchIcall;
#pragma warning restore CS0649

    internal static ulong Call(ulong device, ComputeCommand command, ulong a = 0, ulong b = 0)
        => CallWithData(device, command, a, b, null, 0);

    private static ulong CallWithData(ulong device, ComputeCommand command, ulong a, ulong b,
                                      byte* bytes, uint size)
    {
        if (CommandIcall == null)
            throw new InvalidOperationException("The managed compute runtime is unavailable.");
        ulong result = 0;
        if (CommandIcall(device, (byte)command, a, b, bytes, size, &result) == 0)
            throw new InvalidOperationException($"Compute operation {command} failed; the resource or runtime may be unavailable.");
        return result;
    }

    internal static ulong WithUInt(ulong device, ComputeCommand command, ulong a, ulong b, uint value)
        => CallWithData(device, command, a, b, (byte*)&value, sizeof(uint));

    internal static void GetReadback(ulong device, ulong submission, Span<byte> bytes)
    {
        fixed (byte* data = bytes)
            CallWithData(device, ComputeCommand.GetReadback, submission, 0, data, checked((uint)bytes.Length));
    }
    internal static void Upload(ulong device, ulong buffer, uint offset, ReadOnlySpan<byte> bytes)
    {
        fixed (byte* data = bytes)
            CallWithData(device, ComputeCommand.Upload, buffer, offset, data, checked((uint)bytes.Length));
    }
    internal static void Readback(ulong device, ulong buffer, uint offset, Span<byte> bytes)
    {
        fixed (byte* data = bytes)
            CallWithData(device, ComputeCommand.Readback, buffer, offset, data, checked((uint)bytes.Length));
    }
    internal static ulong Submit(ulong device, ulong pipeline, ReadOnlySpan<NativeComputeBinding> bindings,
                                  uint x, uint y, uint z, ulong arguments, uint offset, ReadOnlySpan<byte> uniforms)
    {
        if (DispatchIcall == null)
            throw new InvalidOperationException("The managed compute runtime is unavailable.");
        ulong result = 0;
        fixed (NativeComputeBinding* resources = bindings)
        fixed (byte* data = uniforms)
        {
            if (DispatchIcall(device, pipeline, resources, checked((uint)bindings.Length), x, y, z, arguments, offset,
                              data, checked((uint)uniforms.Length), &result) == 0)
                throw new InvalidOperationException("Compute dispatch failed; no successful submission was returned.");
        }
        return result;
    }
}
