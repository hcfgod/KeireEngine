using System.Text;

namespace Keire;

internal enum NativeUiScalarProperty : byte
{
    Minimum,
    Maximum,
    Value
}

internal enum NativeUiFlagProperty : byte
{
    Interactable,
    Checked,
    Focused
}

internal enum NativeUiVectorProperty : byte
{
    ScrollOffset,
    ContentSize
}

internal enum NativeUiEventType : byte
{
    PointerEnter,
    PointerExit,
    PointerDown,
    PointerUp,
    Click,
    Focus,
    Blur,
    Submit,
    Cancel,
    ValueChanged,
    TextChanged
}

internal static unsafe class NativeRuntimeUi
{
#pragma warning disable CS0649
    internal static delegate* unmanaged<ulong, ulong, byte, float*, byte> GetScalarIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, float, byte> SetScalarIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte*, byte> GetFlagIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, byte> SetFlagIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, Vector2*, byte> GetVectorIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, Vector2, byte> SetVectorIcall;
    internal static delegate* unmanaged<ulong, ulong, byte*, int, int> GetInputTextIcall;
    internal static delegate* unmanaged<ulong, ulong, NativeString, byte> SetInputTextIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte> ConsumeEventIcall;
    internal static delegate* unmanaged<ulong, ulong, byte> FocusIcall;
#pragma warning restore CS0649

    internal static float GetScalar(Entity entity, NativeUiScalarProperty property)
    {
        float value;
        if (GetScalarIcall(entity.Id.High, entity.Id.Low, (byte)property, &value) == 0)
            throw new InvalidOperationException("The runtime UI scalar property is unavailable.");
        return value;
    }

    internal static void SetScalar(Entity entity, NativeUiScalarProperty property, float value)
    {
        if (SetScalarIcall(entity.Id.High, entity.Id.Low, (byte)property, value) == 0)
            throw new InvalidOperationException("The runtime UI scalar property could not be changed.");
    }

    internal static bool GetFlag(Entity entity, NativeUiFlagProperty property)
    {
        byte value;
        if (GetFlagIcall(entity.Id.High, entity.Id.Low, (byte)property, &value) == 0)
            throw new InvalidOperationException("The runtime UI flag property is unavailable.");
        return value != 0;
    }

    internal static void SetFlag(Entity entity, NativeUiFlagProperty property, bool value)
    {
        if (SetFlagIcall(entity.Id.High, entity.Id.Low, (byte)property, value ? (byte)1 : (byte)0) == 0)
            throw new InvalidOperationException("The runtime UI flag property could not be changed.");
    }

    internal static Vector2 GetVector(Entity entity, NativeUiVectorProperty property)
    {
        Vector2 value;
        if (GetVectorIcall(entity.Id.High, entity.Id.Low, (byte)property, &value) == 0)
            throw new InvalidOperationException("The runtime UI vector property is unavailable.");
        return value;
    }

    internal static void SetVector(Entity entity, NativeUiVectorProperty property, Vector2 value)
    {
        if (SetVectorIcall(entity.Id.High, entity.Id.Low, (byte)property, value) == 0)
            throw new InvalidOperationException("The runtime UI vector property could not be changed.");
    }

    internal static string GetInputText(Entity entity)
    {
        int length = GetInputTextIcall(entity.Id.High, entity.Id.Low, null, 0);
        if (length < 0)
            throw new InvalidOperationException("The runtime UI input text is unavailable.");
        byte[] bytes = new byte[length];
        fixed (byte* destination = bytes)
        {
            if (GetInputTextIcall(entity.Id.High, entity.Id.Low, destination, bytes.Length) != length)
                throw new InvalidOperationException("The runtime UI input text changed while it was read.");
        }
        return Encoding.UTF8.GetString(bytes);
    }

    internal static void SetInputText(Entity entity, string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        using NativeString nativeText = text;
        if (SetInputTextIcall(entity.Id.High, entity.Id.Low, nativeText) == 0)
            throw new InvalidOperationException("The runtime UI input text could not be changed.");
    }

    internal static bool ConsumeEvent(Entity entity, NativeUiEventType type) =>
        ConsumeEventIcall(entity.Id.High, entity.Id.Low, (byte)type) != 0;

    internal static void Focus(Entity entity)
    {
        if (FocusIcall(entity.Id.High, entity.Id.Low) == 0)
            throw new InvalidOperationException("The runtime UI control could not receive focus.");
    }
}
