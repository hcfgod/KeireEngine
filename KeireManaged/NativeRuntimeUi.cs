using System.Runtime.InteropServices;
using System.Text;

namespace Keire;

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

internal enum NativeUiDocumentElementType : byte
{
    Canvas,
    Panel,
    Text,
    Image,
    Button,
    HorizontalLayout,
    VerticalLayout,
    Spacer,
    GridLayout,
    Slider,
    Toggle,
    InputField,
    ScrollView
}

internal enum NativeUiDocumentFlag : byte
{
    Interactable,
    Checked,
    Focused,
    Enabled
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeUiDocumentElement
{
    internal ulong DocumentGeneration;
    internal ulong Element;
    internal ulong StableIdHigh;
    internal ulong StableIdLow;
    internal NativeUiDocumentElementType Type;

    internal readonly AssetId StableId => new(StableIdHigh, StableIdLow);
}

internal static unsafe class NativeRuntimeUi
{
#pragma warning disable CS0649
    internal static delegate* unmanaged<ulong, ulong, NativeUiDocumentElement*, byte> ResolveDocumentRootIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, NativeUiDocumentElement*, byte>
        ResolveDocumentElementByIdIcall;
    internal static delegate* unmanaged<ulong, ulong, NativeString, NativeUiDocumentElement*, byte>
        ResolveDocumentElementByNameIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, byte> DocumentElementAliveIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, byte*, int, int> GetDocumentElementTextIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, NativeString, byte> SetDocumentElementTextIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, float*, byte> GetDocumentElementValueIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, float, byte> SetDocumentElementValueIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, byte, byte*, byte> GetDocumentElementFlagIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, byte, byte, byte> SetDocumentElementFlagIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, byte, byte> ConsumeDocumentElementEventIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, byte> FocusDocumentElementIcall;
#pragma warning restore CS0649

    internal static NativeUiDocumentElement? ResolveDocumentRoot(Entity document)
    {
        NativeUiDocumentElement result;
        return ResolveDocumentRootIcall != null &&
               ResolveDocumentRootIcall(document.Id.High, document.Id.Low, &result) != 0
            ? result
            : null;
    }

    internal static NativeUiDocumentElement? ResolveDocumentElement(Entity document, AssetId stableId)
    {
        NativeUiDocumentElement result;
        return ResolveDocumentElementByIdIcall != null &&
               ResolveDocumentElementByIdIcall(document.Id.High, document.Id.Low, stableId.High, stableId.Low,
                                               &result) != 0
            ? result
            : null;
    }

    internal static NativeUiDocumentElement? ResolveDocumentElement(Entity document, string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        NativeUiDocumentElement result;
        using NativeString nativeName = name;
        return ResolveDocumentElementByNameIcall != null &&
               ResolveDocumentElementByNameIcall(document.Id.High, document.Id.Low, nativeName, &result) != 0
            ? result
            : null;
    }

    internal static bool DocumentElementAlive(Entity document, NativeUiDocumentElement element) =>
        DocumentElementAliveIcall != null &&
        DocumentElementAliveIcall(document.Id.High, document.Id.Low, element.DocumentGeneration, element.Element) != 0;

    internal static string GetDocumentElementText(Entity document, NativeUiDocumentElement element)
    {
        if (GetDocumentElementTextIcall == null)
            throw StaleDocumentElement();
        int length = GetDocumentElementTextIcall(document.Id.High, document.Id.Low, element.DocumentGeneration,
                                                 element.Element, null, 0);
        if (length < 0)
            throw StaleDocumentElement();
        byte[] bytes = new byte[length];
        fixed (byte* destination = bytes)
        {
            if (GetDocumentElementTextIcall(document.Id.High, document.Id.Low, element.DocumentGeneration,
                                            element.Element, destination, bytes.Length) != length)
                throw new InvalidOperationException("The UI Document element text changed while it was read.");
        }
        return Encoding.UTF8.GetString(bytes);
    }

    internal static void SetDocumentElementText(Entity document, NativeUiDocumentElement element, string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        if (SetDocumentElementTextIcall == null)
            throw StaleDocumentElement();
        using NativeString nativeText = text;
        if (SetDocumentElementTextIcall(document.Id.High, document.Id.Low, element.DocumentGeneration,
                                        element.Element, nativeText) == 0)
            throw StaleDocumentElement();
    }

    internal static float GetDocumentElementValue(Entity document, NativeUiDocumentElement element)
    {
        if (GetDocumentElementValueIcall == null)
            throw StaleDocumentElement();
        float value;
        if (GetDocumentElementValueIcall(document.Id.High, document.Id.Low, element.DocumentGeneration,
                                         element.Element, &value) == 0)
            throw StaleDocumentElement();
        return value;
    }

    internal static void SetDocumentElementValue(Entity document, NativeUiDocumentElement element, float value)
    {
        if (SetDocumentElementValueIcall == null)
            throw StaleDocumentElement();
        if (SetDocumentElementValueIcall(document.Id.High, document.Id.Low, element.DocumentGeneration,
                                         element.Element, value) == 0)
            throw StaleDocumentElement();
    }

    internal static bool GetDocumentElementFlag(Entity document, NativeUiDocumentElement element,
                                                NativeUiDocumentFlag property)
    {
        if (GetDocumentElementFlagIcall == null)
            throw StaleDocumentElement();
        byte value;
        if (GetDocumentElementFlagIcall(document.Id.High, document.Id.Low, element.DocumentGeneration,
                                        element.Element, (byte)property, &value) == 0)
            throw StaleDocumentElement();
        return value != 0;
    }

    internal static void SetDocumentElementFlag(Entity document, NativeUiDocumentElement element,
                                                NativeUiDocumentFlag property, bool value)
    {
        if (SetDocumentElementFlagIcall == null)
            throw StaleDocumentElement();
        if (SetDocumentElementFlagIcall(document.Id.High, document.Id.Low, element.DocumentGeneration,
                                        element.Element, (byte)property, value ? (byte)1 : (byte)0) == 0)
            throw StaleDocumentElement();
    }

    internal static bool ConsumeDocumentElementEvent(Entity document, NativeUiDocumentElement element,
                                                     NativeUiEventType type) =>
        ConsumeDocumentElementEventIcall != null &&
        ConsumeDocumentElementEventIcall(document.Id.High, document.Id.Low, element.DocumentGeneration,
                                         element.Element, (byte)type) != 0;

    internal static void FocusDocumentElement(Entity document, NativeUiDocumentElement element)
    {
        if (FocusDocumentElementIcall == null)
            throw StaleDocumentElement();
        if (FocusDocumentElementIcall(document.Id.High, document.Id.Low, element.DocumentGeneration,
                                      element.Element) == 0)
            throw StaleDocumentElement();
    }

    private static InvalidOperationException StaleDocumentElement() =>
        new("The UI Document element is unavailable because its document was destroyed or reloaded.");
}
