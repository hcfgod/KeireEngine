using System.Runtime.InteropServices;
using System.Text;

namespace Keire;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeInputDevice
{
    internal uint Id;
    internal byte Type;
    internal byte ConnectedValue;
    internal byte PairedValue;
    internal byte Reserved;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeInputRebindSnapshot
{
    internal ulong BindingHigh;
    internal ulong BindingLow;
    internal double RemainingSeconds;
    internal uint ConflictCount;
    internal byte Status;
    private fixed byte _reserved[3];
}

internal static unsafe class NativeInput
{
#pragma warning disable CS0649
    internal static delegate* unmanaged<int> GetDeviceCountIcall;
    internal static delegate* unmanaged<int, NativeInputDevice*, byte> GetDeviceIcall;
    internal static delegate* unmanaged<uint, byte*, int, int> GetDeviceNameIcall;
    internal static delegate* unmanaged<byte*, int, int> GetControlSchemeIcall;
    internal static delegate* unmanaged<NativeString, byte, byte> SetControlSchemeIcall;
    internal static delegate* unmanaged<byte> ClearControlSchemeLockIcall;
    internal static delegate* unmanaged<uint, float, float, float, byte> SetGamepadRumbleIcall;
    internal static delegate* unmanaged<ulong, ulong, float, double, byte, ulong> BeginRebindIcall;
    internal static delegate* unmanaged<ulong, NativeInputRebindSnapshot*, byte> GetRebindSnapshotIcall;
    internal static delegate* unmanaged<ulong, byte*, int, int> GetRebindCandidateIcall;
    internal static delegate* unmanaged<ulong, byte, byte> ResolveRebindIcall;
    internal static delegate* unmanaged<ulong, byte> CancelRebindIcall;
    internal static delegate* unmanaged<NativeString, byte> SaveBindingsIcall;
    internal static delegate* unmanaged<NativeString, int> LoadBindingsIcall;
    internal static delegate* unmanaged<byte> ClearBindingsIcall;
#pragma warning restore CS0649

    internal static IReadOnlyList<InputDevice> Devices
    {
        get
        {
            RequireBound();
            int count = GetDeviceCountIcall();
            if (count < 0 || count > 32)
                throw new InvalidOperationException("The native runtime returned an invalid input device count.");
            if (count == 0)
                return Array.Empty<InputDevice>();
            var result = new InputDevice[count];
            for (int index = 0; index < count; ++index)
            {
                NativeInputDevice native = default;
                if (GetDeviceIcall(index, &native) == 0 || native.Id == 0 || native.Type > (byte)InputDeviceType.Gamepad)
                    throw new InvalidOperationException("The native runtime returned an invalid input device.");
                result[index] = new InputDevice(native.Id, (InputDeviceType)native.Type, ReadDeviceName(native.Id),
                                                native.ConnectedValue != 0, native.PairedValue != 0);
            }
            return result;
        }
    }

    internal static string ControlScheme
    {
        get
        {
            RequireBound();
            return ReadText(GetControlSchemeIcall, 1024, "input control scheme");
        }
    }

    internal static bool SetControlScheme(string scheme, bool locked)
    {
        RequireBound();
        using NativeString native = scheme;
        return SetControlSchemeIcall(native, locked ? (byte)1 : (byte)0) != 0;
    }

    internal static bool ClearControlSchemeLock()
    {
        RequireBound();
        return ClearControlSchemeLockIcall() != 0;
    }

    internal static bool SetGamepadRumble(uint device, float lowFrequency, float highFrequency, float durationSeconds)
    {
        RequireBound();
        return SetGamepadRumbleIcall(device, lowFrequency, highFrequency, durationSeconds) != 0;
    }

    internal static ulong BeginRebind(AssetId binding, InputRebindOptions options)
    {
        RequireBound();
        return BeginRebindIcall(binding.High, binding.Low, options.MagnitudeThreshold, options.TimeoutSeconds,
                                (byte)options.AllowedDevices);
    }

    internal static InputRebindSnapshot RebindSnapshot(ulong operation)
    {
        RequireBound();
        NativeInputRebindSnapshot native = default;
        if (GetRebindSnapshotIcall(operation, &native) == 0 || native.Status > (byte)InputRebindStatus.TimedOut)
            throw new InvalidOperationException("The input rebind operation is no longer available.");
        return new InputRebindSnapshot(new AssetId(native.BindingHigh, native.BindingLow),
                                       (InputRebindStatus)native.Status, ReadRebindCandidate(operation),
                                       native.RemainingSeconds, native.ConflictCount);
    }

    internal static bool ResolveRebind(ulong operation, InputRebindResolution resolution)
    {
        RequireBound();
        return ResolveRebindIcall(operation, (byte)resolution) != 0;
    }

    internal static bool CancelRebind(ulong operation)
    {
        RequireBound();
        return CancelRebindIcall(operation) != 0;
    }

    internal static bool SaveBindings(string profile)
    {
        RequireBound();
        using NativeString native = profile;
        return SaveBindingsIcall(native) != 0;
    }

    internal static int LoadBindings(string profile)
    {
        RequireBound();
        using NativeString native = profile;
        return LoadBindingsIcall(native);
    }

    internal static bool ClearBindings()
    {
        RequireBound();
        return ClearBindingsIcall() != 0;
    }

    private static string ReadDeviceName(uint device)
    {
        int length = GetDeviceNameIcall(device, null, 0);
        if (length < 0 || length > 4096)
            throw new InvalidOperationException("The native runtime returned an invalid input device name.");
        if (length == 0)
            return string.Empty;
        var bytes = new byte[length];
        fixed (byte* destination = bytes)
        {
            if (GetDeviceNameIcall(device, destination, bytes.Length) != bytes.Length)
                throw new InvalidOperationException("The input device changed while its name was being read.");
        }
        return Encoding.UTF8.GetString(bytes);
    }

    private static string ReadRebindCandidate(ulong operation)
    {
        int length = GetRebindCandidateIcall(operation, null, 0);
        if (length < 0 || length > 4096)
            throw new InvalidOperationException("The native runtime returned an invalid rebind candidate.");
        if (length == 0)
            return string.Empty;
        var bytes = new byte[length];
        fixed (byte* destination = bytes)
        {
            if (GetRebindCandidateIcall(operation, destination, bytes.Length) != bytes.Length)
                throw new InvalidOperationException("The rebind candidate changed while it was being read.");
        }
        return Encoding.UTF8.GetString(bytes);
    }

    private static string ReadText(delegate* unmanaged<byte*, int, int> reader, int limit, string label)
    {
        int length = reader(null, 0);
        if (length < 0 || length > limit)
            throw new InvalidOperationException($"The native runtime returned an invalid {label}.");
        if (length == 0)
            return string.Empty;
        var bytes = new byte[length];
        fixed (byte* destination = bytes)
        {
            if (reader(destination, bytes.Length) != bytes.Length)
                throw new InvalidOperationException($"The {label} changed while it was being read.");
        }
        return Encoding.UTF8.GetString(bytes);
    }

    private static void RequireBound()
    {
        if (GetDeviceCountIcall == null || GetDeviceIcall == null || GetDeviceNameIcall == null ||
            GetControlSchemeIcall == null || SetControlSchemeIcall == null || ClearControlSchemeLockIcall == null ||
            SetGamepadRumbleIcall == null || BeginRebindIcall == null || GetRebindSnapshotIcall == null ||
            GetRebindCandidateIcall == null || ResolveRebindIcall == null || CancelRebindIcall == null ||
            SaveBindingsIcall == null || LoadBindingsIcall == null || ClearBindingsIcall == null)
        {
            throw new InvalidOperationException("Kéire managed input is not attached.");
        }
    }
}
