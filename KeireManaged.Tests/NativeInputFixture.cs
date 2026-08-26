using System.Text;

internal static unsafe class NativeInputFixture
{
    internal static int SetSchemeCalls;
    internal static int RumbleCalls;
    internal static int BeginRebindCalls;
    internal static int ResolveCalls;
    internal static int PersistenceCalls;
    internal static int ActionSnapshotCalls;
    internal static byte LastContextOperation;
    internal static Keire.AssetId LastContextTarget;
    internal static Keire.InputRebindResolution Resolution;
    internal static float LowFrequency;
    internal static float HighFrequency;
    internal static float DurationSeconds;

    internal static void Install()
    {
        SetSchemeCalls = 0;
        RumbleCalls = 0;
        BeginRebindCalls = 0;
        ResolveCalls = 0;
        PersistenceCalls = 0;
        ActionSnapshotCalls = 0;
        LastContextOperation = byte.MaxValue;
        LastContextTarget = default;
        Resolution = default;
        LowFrequency = 0.0f;
        HighFrequency = 0.0f;
        DurationSeconds = 0.0f;
        Keire.NativeInput.GetDeviceCountIcall = &GetDeviceCount;
        Keire.NativeInput.GetDeviceIcall = &GetDevice;
        Keire.NativeInput.GetDeviceNameIcall = &GetDeviceName;
        Keire.NativeInput.GetControlSchemeIcall = &GetControlScheme;
        Keire.NativeInput.SetControlSchemeIcall = &SetControlScheme;
        Keire.NativeInput.ClearControlSchemeLockIcall = &ClearControlSchemeLock;
        Keire.NativeInput.SetGamepadRumbleIcall = &SetGamepadRumble;
        Keire.NativeInput.BeginRebindIcall = &BeginRebind;
        Keire.NativeInput.GetRebindSnapshotIcall = &GetRebindSnapshot;
        Keire.NativeInput.GetRebindCandidateIcall = &GetRebindCandidate;
        Keire.NativeInput.ResolveRebindIcall = &ResolveRebind;
        Keire.NativeInput.CancelRebindIcall = &CancelRebind;
        Keire.NativeInput.SaveBindingsIcall = &SaveBindings;
        Keire.NativeInput.LoadBindingsIcall = &LoadBindings;
        Keire.NativeInput.ClearBindingsIcall = &ClearBindings;
        Keire.NativeInput.CreateContextIcall = &CreateContext;
        Keire.NativeInput.ReleaseContextIcall = &ReleaseContext;
        Keire.NativeInput.OperateContextIcall = &OperateContext;
        Keire.NativeInput.BeginContextRebindIcall = &BeginContextRebind;
        Keire.NativeInput.FindMapIcall = &FindMap;
        Keire.NativeInput.FindActionIcall = &FindAction;
        Keire.NativeInput.GetActionSnapshotIcall = &GetActionSnapshot;
        Keire.NativeInput.GetCurrentDeviceIcall = &GetCurrentDevice;
        Keire.NativeInput.GetControlSnapshotIcall = &GetControlSnapshot;
    }

    internal static void Uninstall()
    {
        Keire.NativeInput.GetDeviceCountIcall = null;
        Keire.NativeInput.GetDeviceIcall = null;
        Keire.NativeInput.GetDeviceNameIcall = null;
        Keire.NativeInput.GetControlSchemeIcall = null;
        Keire.NativeInput.SetControlSchemeIcall = null;
        Keire.NativeInput.ClearControlSchemeLockIcall = null;
        Keire.NativeInput.SetGamepadRumbleIcall = null;
        Keire.NativeInput.BeginRebindIcall = null;
        Keire.NativeInput.GetRebindSnapshotIcall = null;
        Keire.NativeInput.GetRebindCandidateIcall = null;
        Keire.NativeInput.ResolveRebindIcall = null;
        Keire.NativeInput.CancelRebindIcall = null;
        Keire.NativeInput.SaveBindingsIcall = null;
        Keire.NativeInput.LoadBindingsIcall = null;
        Keire.NativeInput.ClearBindingsIcall = null;
        Keire.NativeInput.CreateContextIcall = null;
        Keire.NativeInput.ReleaseContextIcall = null;
        Keire.NativeInput.OperateContextIcall = null;
        Keire.NativeInput.BeginContextRebindIcall = null;
        Keire.NativeInput.FindMapIcall = null;
        Keire.NativeInput.FindActionIcall = null;
        Keire.NativeInput.GetActionSnapshotIcall = null;
        Keire.NativeInput.GetCurrentDeviceIcall = null;
        Keire.NativeInput.GetControlSnapshotIcall = null;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetDeviceCount() => 2;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetDevice(int index, Keire.NativeInputDevice* destination)
    {
        if (destination == null || index < 0 || index > 1)
            return 0;
        *destination = index == 0
            ? new Keire.NativeInputDevice { Id = 1, Type = (byte)Keire.InputDeviceType.Keyboard, ConnectedValue = 1,
                                            PairedValue = 1 }
            : new Keire.NativeInputDevice { Id = 7, Type = (byte)Keire.InputDeviceType.Gamepad, ConnectedValue = 1,
                                            PairedValue = 1 };
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetDeviceName(uint device, byte* destination, int capacity) =>
        CopyText(device == 1 ? "Keyboard" : device == 7 ? "Pro Controller" : null, destination, capacity);

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetControlScheme(byte* destination, int capacity) => CopyText("Gamepad", destination, capacity);

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetControlScheme(Keire.NativeString scheme, byte locked)
    {
        ++SetSchemeCalls;
        return locked;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ClearControlSchemeLock() => 1;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetGamepadRumble(uint device, float lowFrequency, float highFrequency, float durationSeconds)
    {
        ++RumbleCalls;
        LowFrequency = lowFrequency;
        HighFrequency = highFrequency;
        DurationSeconds = durationSeconds;
        return device == 7 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static ulong BeginRebind(ulong bindingHigh, ulong bindingLow, float threshold, double timeoutSeconds,
                                     byte allowedDevices)
    {
        ++BeginRebindCalls;
        return bindingHigh == 71 && bindingLow == 73 && threshold == 0.65f && timeoutSeconds == 8.0 &&
               allowedDevices == (byte)Keire.InputDeviceMask.Gamepad
            ? 99UL
            : 0UL;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetRebindSnapshot(ulong operation, Keire.NativeInputRebindSnapshot* destination)
    {
        if (operation != 99 || destination == null)
            return 0;
        destination->BindingHigh = 71;
        destination->BindingLow = 73;
        destination->RemainingSeconds = 3.5;
        destination->ConflictCount = 2;
        destination->Status = (byte)Keire.InputRebindStatus.Candidate;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetRebindCandidate(ulong operation, byte* destination, int capacity) =>
        CopyText(operation == 99 ? "<Gamepad>/buttonSouth" : null, destination, capacity);

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ResolveRebind(ulong operation, byte resolution)
    {
        ++ResolveCalls;
        Resolution = (Keire.InputRebindResolution)resolution;
        return operation == 99 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte CancelRebind(ulong operation) => operation == 99 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SaveBindings(Keire.NativeString profile)
    {
        ++PersistenceCalls;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int LoadBindings(Keire.NativeString profile)
    {
        ++PersistenceCalls;
        return 3;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ClearBindings()
    {
        ++PersistenceCalls;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static ulong CreateContext(ulong generation, ulong assetHigh, ulong assetLow) =>
        generation == 8101 && assetHigh == 31 && assetLow == 37 ? 101UL : 0UL;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ReleaseContext(ulong context) => context == 101 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte OperateContext(ulong context, byte operation, ulong targetHigh, ulong targetLow)
    {
        LastContextOperation = operation;
        LastContextTarget = new Keire.AssetId(targetHigh, targetLow);
        return context == 101 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static ulong BeginContextRebind(ulong context, ulong bindingHigh, ulong bindingLow, float threshold,
                                            double timeoutSeconds, byte allowedDevices) =>
        context == 101 && bindingHigh == 71 && bindingLow == 73 ? 99UL : 0UL;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte FindMap(ulong context, Keire.NativeString name, ulong* high, ulong* low)
    {
        if (context != 101 || high == null || low == null)
            return 0;
        *high = 41;
        *low = 43;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte FindAction(ulong context, ulong mapHigh, ulong mapLow, Keire.NativeString name,
                                   ulong* high, ulong* low)
    {
        if (context != 101 || mapHigh != 41 || mapLow != 43 || high == null || low == null)
            return 0;
        *high = 47;
        *low = 53;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetActionSnapshot(ulong context, ulong actionHigh, ulong actionLow,
                                           Keire.NativeInputActionSnapshot* destination)
    {
        ++ActionSnapshotCalls;
        if (context != 101 || actionHigh != 47 || actionLow != 53 || destination == null)
            return 0;
        *destination = new Keire.NativeInputActionSnapshot
        {
            Frame = 8,
            X = 1.0f,
            Phase = (byte)Keire.InputActionPhase.Performed,
            ValueType = (byte)Keire.InputValueType.Boolean,
            EnabledValue = 1,
            StartedValue = 1,
            PerformedValue = 1
        };
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static uint GetCurrentDevice(byte type) => type switch
    {
        (byte)Keire.InputDeviceType.Keyboard => 1,
        (byte)Keire.InputDeviceType.Mouse => 2,
        (byte)Keire.InputDeviceType.Gamepad => 7,
        _ => 0
    };

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetControlSnapshot(uint device, Keire.NativeString path,
                                           Keire.NativeInputControlSnapshot* destination)
    {
        if (device != 1 || destination == null)
            return 0;
        *destination = new Keire.NativeInputControlSnapshot
        {
            Frame = 8,
            Device = 1,
            ValueType = (byte)Keire.InputValueType.Boolean,
            X = 1.0f,
            PressedValue = 1
        };
        return 1;
    }

    private static int CopyText(string? value, byte* destination, int capacity)
    {
        if (value == null || capacity < 0)
            return -1;
        int length = Encoding.UTF8.GetByteCount(value);
        if (destination == null || capacity == 0)
            return length;
        if (capacity < length)
            return -1;
        return Encoding.UTF8.GetBytes(value, new Span<byte>(destination, capacity));
    }
}
