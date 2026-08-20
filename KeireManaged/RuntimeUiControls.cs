namespace Keire;

public abstract class NativeUiControl : UiElement
{
    protected NativeUiControl(Entity entity)
    {
        Entity = entity;
        Interactable = true;
    }

    public Entity Entity { get; }
    public bool NativeBound => Entity.Id != default;
    public bool IsFocused => NativeRuntimeUi.GetFlag(Entity, NativeUiFlagProperty.Focused);

    public void Focus() => NativeRuntimeUi.Focus(Entity);

    protected bool NativeInteractable
    {
        get => NativeRuntimeUi.GetFlag(Entity, NativeUiFlagProperty.Interactable);
        set => NativeRuntimeUi.SetFlag(Entity, NativeUiFlagProperty.Interactable, value);
    }
}

public sealed class UiSlider : NativeUiControl
{
    internal UiSlider(Entity entity) : base(entity) { }

    public bool IsValid => NativeBound && Entity.HasComponent<UiSliderComponent>();
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
    public new bool Interactable
    {
        get => NativeInteractable;
        set => NativeInteractable = value;
    }
    public bool ChangedThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.ValueChanged);

    public static UiSlider? FromEntity(Entity entity) =>
        entity.HasComponent<UiSliderComponent>() ? new UiSlider(entity) : null;
}

public sealed class UiToggle : NativeUiControl
{
    internal UiToggle(Entity entity) : base(entity) { }

    public bool IsValid => NativeBound && Entity.HasComponent<UiToggleComponent>();
    public bool IsOn
    {
        get => NativeRuntimeUi.GetFlag(Entity, NativeUiFlagProperty.Checked);
        set => NativeRuntimeUi.SetFlag(Entity, NativeUiFlagProperty.Checked, value);
    }
    public new bool Interactable
    {
        get => NativeInteractable;
        set => NativeInteractable = value;
    }
    public bool ChangedThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.ValueChanged);

    public static UiToggle? FromEntity(Entity entity) =>
        entity.HasComponent<UiToggleComponent>() ? new UiToggle(entity) : null;
}

public sealed class UiInputField : NativeUiControl
{
    internal UiInputField(Entity entity) : base(entity) { }

    public bool IsValid => NativeBound && Entity.HasComponent<UiInputFieldComponent>();
    public string Text
    {
        get => NativeRuntimeUi.GetInputText(Entity);
        set => NativeRuntimeUi.SetInputText(Entity, value);
    }
    public new bool Interactable
    {
        get => NativeInteractable;
        set => NativeInteractable = value;
    }
    public bool ChangedThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.TextChanged);
    public bool SubmittedThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.Submit);
    public bool CancelledThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.Cancel);

    public static UiInputField? FromEntity(Entity entity) =>
        entity.HasComponent<UiInputFieldComponent>() ? new UiInputField(entity) : null;
}

public sealed class UiScrollView : NativeUiControl
{
    internal UiScrollView(Entity entity) : base(entity) { }

    public bool IsValid => NativeBound && Entity.HasComponent<UiScrollViewComponent>();
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
    public new bool Interactable
    {
        get => NativeInteractable;
        set => NativeInteractable = value;
    }
    public bool ScrolledThisFrame => NativeRuntimeUi.ConsumeEvent(Entity, NativeUiEventType.ValueChanged);

    public static UiScrollView? FromEntity(Entity entity) =>
        entity.HasComponent<UiScrollViewComponent>() ? new UiScrollView(entity) : null;
}
