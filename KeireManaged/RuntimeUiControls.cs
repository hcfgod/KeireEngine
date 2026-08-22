namespace Keire;

public enum CanvasScaleMode : byte { ConstantPixels, ScaleWithViewport, ConstantPhysicalSize }
public enum UiTextAlignment : byte
{
    UpperLeft, UpperCenter, UpperRight, MiddleLeft, MiddleCenter, MiddleRight, LowerLeft, LowerCenter, LowerRight
}
public enum UiImageType : byte { Simple, Sliced, Tiled, Filled }
public enum UiButtonTransition : byte { None, ColorTint, SpriteSwap, Animation }
public enum UiLayoutDirection : byte { Horizontal, Vertical, Grid }
public enum UiSliderDirection : byte { LeftToRight, RightToLeft, BottomToTop, TopToBottom }
public enum UiInputContentType : byte { Standard, Integer, Decimal, Password }
public enum UiAccessibilityRole : byte { Automatic, Button, Slider, Toggle, TextBox, ScrollView, Text, Image }

public abstract class NativeUiControl : Component
{
    protected NativeUiControl(Entity entity) : base(entity) { }

    public bool IsFocused => NativeRuntimeUi.GetFlag(Entity, NativeUiFlagProperty.Focused);
    public bool Interactable
    {
        get => NativeRuntimeUi.GetFlag(Entity, NativeUiFlagProperty.Interactable);
        set => NativeRuntimeUi.SetFlag(Entity, NativeUiFlagProperty.Interactable, value);
    }
    public void Focus() => NativeRuntimeUi.Focus(Entity);
}

[StableComponentId("4b454952-4555-4943-414e-564153000001")]
public sealed class Canvas : Component
{
    internal Canvas(Entity entity) : base(entity) { }
    public Vector2 ReferenceResolution { get => GetBuiltinVector2("referenceResolution"); set => SetBuiltinVector2("referenceResolution", value); }
    public CanvasScaleMode ScaleMode { get => (CanvasScaleMode)GetBuiltinInteger("scaleMode"); set => SetBuiltinInteger("scaleMode", (long)value); }
    public float MatchWidthOrHeight { get => GetBuiltinScalar("match"); set => SetBuiltinScalar("match", value); }
    public float AccessibilityScale { get => GetBuiltinScalar("accessibilityScale"); set => SetBuiltinScalar("accessibilityScale", value); }
    public int SortingOrder { get => checked((int)GetBuiltinInteger("sortingOrder")); set => SetBuiltinInteger("sortingOrder", value); }
    public bool RespectSafeArea { get => GetBuiltinBoolean("respectSafeArea"); set => SetBuiltinBoolean("respectSafeArea", value); }
    public bool PixelPerfect { get => GetBuiltinBoolean("pixelPerfect"); set => SetBuiltinBoolean("pixelPerfect", value); }
}

[StableComponentId("4b454952-4555-4952-4543-545452410001")]
public sealed class RectTransform : Component
{
    internal RectTransform(Entity entity) : base(entity) { }
    public Vector2 AnchorMinimum { get => GetBuiltinVector2("anchorMinimum"); set => SetBuiltinVector2("anchorMinimum", value); }
    public Vector2 AnchorMaximum { get => GetBuiltinVector2("anchorMaximum"); set => SetBuiltinVector2("anchorMaximum", value); }
    public Vector2 Pivot { get => GetBuiltinVector2("pivot"); set => SetBuiltinVector2("pivot", value); }
    public Vector2 AnchoredPosition { get => GetBuiltinVector2("anchoredPosition"); set => SetBuiltinVector2("anchoredPosition", value); }
    public Vector2 SizeDelta { get => GetBuiltinVector2("sizeDelta"); set => SetBuiltinVector2("sizeDelta", value); }
    public float Rotation { get => GetBuiltinScalar("rotation"); set => SetBuiltinScalar("rotation", value); }
    public Vector2 Scale { get => GetBuiltinVector2("scale"); set => SetBuiltinVector2("scale", value); }
}

[StableComponentId("4b454952-4555-4954-4558-540000000001")]
public sealed class UiText : Component
{
    internal UiText(Entity entity) : base(entity) { }
    public string Text { get => GetBuiltinText("text"); set => SetBuiltinText("text", value); }
    public Color Color { get => GetBuiltinColor("color"); set => SetBuiltinColor("color", value); }
    public float FontSize { get => GetBuiltinScalar("fontSize"); set => SetBuiltinScalar("fontSize", value); }
    public UiTextAlignment Alignment { get => (UiTextAlignment)GetBuiltinInteger("alignment"); set => SetBuiltinInteger("alignment", (long)value); }
    public bool Wrap { get => GetBuiltinBoolean("wrap"); set => SetBuiltinBoolean("wrap", value); }
    public bool RichText { get => GetBuiltinBoolean("richText"); set => SetBuiltinBoolean("richText", value); }
    public bool RaycastTarget { get => GetBuiltinBoolean("raycastTarget"); set => SetBuiltinBoolean("raycastTarget", value); }
    public bool SetText(string text) { Text = text; return true; }
}

[StableComponentId("4b454952-4555-4949-4d41-474500000001")]
public sealed class UiImage : Component
{
    internal UiImage(Entity entity) : base(entity) { }
    public Texture? Sprite { get => GetBuiltinAsset<Texture>("sprite"); set => SetBuiltinAsset("sprite", value); }
    public Color Tint { get => GetBuiltinColor("tint"); set => SetBuiltinColor("tint", value); }
    public UiImageType ImageType { get => (UiImageType)GetBuiltinInteger("imageType"); set => SetBuiltinInteger("imageType", (long)value); }
    public float FillAmount { get => GetBuiltinScalar("fillAmount"); set => SetBuiltinScalar("fillAmount", value); }
    public float PixelsPerUnit { get => GetBuiltinScalar("pixelsPerUnit"); set => SetBuiltinScalar("pixelsPerUnit", value); }
    public bool PreserveAspect { get => GetBuiltinBoolean("preserveAspect"); set => SetBuiltinBoolean("preserveAspect", value); }
    public bool RaycastTarget { get => GetBuiltinBoolean("raycastTarget"); set => SetBuiltinBoolean("raycastTarget", value); }
}

[StableComponentId("4b454952-4555-4942-5554-544f4e000001")]
public sealed class UiButton : NativeUiControl
{
    private RuntimeUiButton? _events;

    internal UiButton(Entity entity) : base(entity) { }
    public UiButtonTransition Transition { get => (UiButtonTransition)GetBuiltinInteger("transition"); set => SetBuiltinInteger("transition", (long)value); }
    public Color NormalColor { get => GetBuiltinColor("normalColor"); set => SetBuiltinColor("normalColor", value); }
    public Color HoverColor { get => GetBuiltinColor("hoverColor"); set => SetBuiltinColor("hoverColor", value); }
    public Color PressedColor { get => GetBuiltinColor("pressedColor"); set => SetBuiltinColor("pressedColor", value); }
    public Color DisabledColor { get => GetBuiltinColor("disabledColor"); set => SetBuiltinColor("disabledColor", value); }
    public float TransitionDuration { get => GetBuiltinScalar("transitionDuration"); set => SetBuiltinScalar("transitionDuration", value); }
    public string Action { get => GetBuiltinText("action"); set => SetBuiltinText("action", value); }
    public event Action? Clicked
    {
        add
        {
            _events ??= RuntimeUiButton.FromEntity(Entity);
            if (_events is not null)
                _events.Clicked += value;
        }
        remove
        {
            if (_events is not null)
                _events.Clicked -= value;
        }
    }
    public bool ClickedThisFrame => NativeRuntime.ConsumeUiClick(Entity);
}

[StableComponentId("4b454952-4555-494c-4159-4f5554000001")]
public sealed class UiLayout : Component
{
    internal UiLayout(Entity entity) : base(entity) { }
    public UiLayoutDirection Direction { get => (UiLayoutDirection)GetBuiltinInteger("direction"); set => SetBuiltinInteger("direction", (long)value); }
    public Vector4 Padding { get => GetBuiltinVector4("padding"); set => SetBuiltinVector4("padding", value); }
    public Vector2 CellSize { get => GetBuiltinVector2("cellSize"); set => SetBuiltinVector2("cellSize", value); }
    public float Spacing { get => GetBuiltinScalar("spacing"); set => SetBuiltinScalar("spacing", value); }
    public int Alignment { get => checked((int)GetBuiltinInteger("alignment")); set => SetBuiltinInteger("alignment", value); }
    public bool ControlChildWidth { get => GetBuiltinBoolean("controlChildWidth"); set => SetBuiltinBoolean("controlChildWidth", value); }
    public bool ControlChildHeight { get => GetBuiltinBoolean("controlChildHeight"); set => SetBuiltinBoolean("controlChildHeight", value); }
    public bool ForceExpandWidth { get => GetBuiltinBoolean("forceExpandWidth"); set => SetBuiltinBoolean("forceExpandWidth", value); }
    public bool ForceExpandHeight { get => GetBuiltinBoolean("forceExpandHeight"); set => SetBuiltinBoolean("forceExpandHeight", value); }
}

[StableComponentId("4b454952-4555-4953-4c49-444552000001")]
public sealed class UiSlider : NativeUiControl
{
    internal UiSlider(Entity entity) : base(entity) { }
    public float Minimum
    {
        get => NativeRuntimeUi.GetScalar(Entity, NativeUiScalarProperty.Minimum);
        set => NativeRuntimeUi.SetScalar(Entity, NativeUiScalarProperty.Minimum, value);
    }
    public float Maximum
    {
        get => NativeRuntimeUi.GetScalar(Entity, NativeUiScalarProperty.Maximum);
        set => NativeRuntimeUi.SetScalar(Entity, NativeUiScalarProperty.Maximum, value);
    }
    public float Value
    {
        get => NativeRuntimeUi.GetScalar(Entity, NativeUiScalarProperty.Value);
        set => NativeRuntimeUi.SetScalar(Entity, NativeUiScalarProperty.Value, value);
    }
    public float Step { get => GetBuiltinScalar("step"); set => SetBuiltinScalar("step", value); }
    public bool WholeNumbers { get => GetBuiltinBoolean("wholeNumbers"); set => SetBuiltinBoolean("wholeNumbers", value); }
    public UiSliderDirection Direction { get => (UiSliderDirection)GetBuiltinInteger("direction"); set => SetBuiltinInteger("direction", (long)value); }
    public bool ChangedThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.ValueChanged);
}

[StableComponentId("4b454952-4555-4954-4f47-474c45000001")]
public sealed class UiToggle : NativeUiControl
{
    internal UiToggle(Entity entity) : base(entity) { }
    public bool IsOn
    {
        get => NativeRuntimeUi.GetFlag(Entity, NativeUiFlagProperty.Checked);
        set => NativeRuntimeUi.SetFlag(Entity, NativeUiFlagProperty.Checked, value);
    }
    public Color OnColor { get => GetBuiltinColor("onColor"); set => SetBuiltinColor("onColor", value); }
    public Color OffColor { get => GetBuiltinColor("offColor"); set => SetBuiltinColor("offColor", value); }
    public bool ChangedThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.ValueChanged);
}

[StableComponentId("4b454952-4555-4949-4e50-5554464c4401")]
public sealed class UiInputField : NativeUiControl
{
    internal UiInputField(Entity entity) : base(entity) { }
    public string Text
    {
        get => NativeRuntimeUi.GetInputText(Entity);
        set => NativeRuntimeUi.SetInputText(Entity, value);
    }
    public string Placeholder { get => GetBuiltinText("placeholder"); set => SetBuiltinText("placeholder", value); }
    public uint CharacterLimit { get => checked((uint)GetBuiltinInteger("characterLimit")); set => SetBuiltinInteger("characterLimit", value); }
    public UiInputContentType ContentType { get => (UiInputContentType)GetBuiltinInteger("contentType"); set => SetBuiltinInteger("contentType", (long)value); }
    public bool Multiline { get => GetBuiltinBoolean("multiline"); set => SetBuiltinBoolean("multiline", value); }
    public bool ChangedThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.TextChanged);
    public bool SubmittedThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.Submit);
    public bool CancelledThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.Cancel);
}

[StableComponentId("4b454952-4555-4953-4352-4f4c4c000001")]
public sealed class UiScrollView : NativeUiControl
{
    internal UiScrollView(Entity entity) : base(entity) { }
    public Vector2 Offset
    {
        get => NativeRuntimeUi.GetVector(Entity, NativeUiVectorProperty.ScrollOffset);
        set => NativeRuntimeUi.SetVector(Entity, NativeUiVectorProperty.ScrollOffset, value);
    }
    public Vector2 ContentSize
    {
        get => NativeRuntimeUi.GetVector(Entity, NativeUiVectorProperty.ContentSize);
        set => NativeRuntimeUi.SetVector(Entity, NativeUiVectorProperty.ContentSize, value);
    }
    public float Sensitivity { get => GetBuiltinScalar("sensitivity"); set => SetBuiltinScalar("sensitivity", value); }
    public bool Horizontal { get => GetBuiltinBoolean("horizontal"); set => SetBuiltinBoolean("horizontal", value); }
    public bool Vertical { get => GetBuiltinBoolean("vertical"); set => SetBuiltinBoolean("vertical", value); }
    public bool ScrolledThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.ValueChanged);
}

[StableComponentId("4b454952-4555-4941-4343-455353000001")]
public sealed class UiAccessibility : Component
{
    internal UiAccessibility(Entity entity) : base(entity) { }
    public string Label { get => GetBuiltinText("label"); set => SetBuiltinText("label", value); }
    public string Hint { get => GetBuiltinText("hint"); set => SetBuiltinText("hint", value); }
    public UiAccessibilityRole Role { get => (UiAccessibilityRole)GetBuiltinInteger("role"); set => SetBuiltinInteger("role", (long)value); }
    public int NavigationOrder { get => checked((int)GetBuiltinInteger("navigationOrder")); set => SetBuiltinInteger("navigationOrder", value); }
}
