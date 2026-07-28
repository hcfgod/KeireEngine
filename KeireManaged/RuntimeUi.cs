namespace Keire;

public enum UiScaleMode
{
    ConstantPixels,
    ScaleWithScreen,
    ConstantPhysicalSize
}

public enum UiAxisAlignment
{
    Start,
    Center,
    End,
    Stretch
}

public readonly record struct UiRect(float X, float Y, float Width, float Height)
{
    public float Right => X + Width;
    public float Bottom => Y + Height;
}

public readonly record struct UiThickness(float Left, float Top, float Right, float Bottom)
{
    public UiThickness(float uniform) : this(uniform, uniform, uniform, uniform) { }
}

public abstract class UiElement
{
    private readonly List<UiElement> _children = [];

    public string Name { get; set; } = string.Empty;
    public bool Visible { get; set; } = true;
    public bool Interactable { get; set; }
    public Vector2 AnchorMinimum { get; set; }
    public Vector2 AnchorMaximum { get; set; }
    public Vector2 Pivot { get; set; } = new(0.5f, 0.5f);
    public Vector2 Position { get; set; }
    public Vector2 Size { get; set; } = new(100.0f, 30.0f);
    public UiThickness Margin { get; set; }
    public UiRect LayoutRect { get; internal set; }
    public UiElement? Parent { get; private set; }
    public IReadOnlyList<UiElement> Children => _children;

    public void Add(UiElement child)
    {
        ArgumentNullException.ThrowIfNull(child);
        if (ReferenceEquals(child, this) || IsAncestorOf(child))
            throw new InvalidOperationException("Runtime UI hierarchy cannot contain cycles.");
        child.Parent?._children.Remove(child);
        child.Parent = this;
        _children.Add(child);
    }

    public bool Remove(UiElement child)
    {
        if (!_children.Remove(child))
            return false;
        child.Parent = null;
        return true;
    }

    private bool IsAncestorOf(UiElement candidate)
    {
        for (UiElement? current = Parent; current is not null; current = current.Parent)
            if (ReferenceEquals(current, candidate))
                return true;
        return false;
    }
}

public sealed class UiPanel : UiElement
{
    public Color Color { get; set; } = new(0.0f, 0.0f, 0.0f, 0.65f);
    public UiThickness Padding { get; set; }
    public bool ClipChildren { get; set; }
}

public sealed class UiText : UiElement
{
    public string Text { get; set; } = string.Empty;
    public float FontSize { get; set; } = 18.0f;
    public Color Color { get; set; } = Color.White;
    public UiAxisAlignment HorizontalAlignment { get; set; } = UiAxisAlignment.Start;
    public UiAxisAlignment VerticalAlignment { get; set; } = UiAxisAlignment.Center;
}

public sealed class UiImage : UiElement
{
    public AssetReference<ScriptableObject> Texture { get; set; }
    public Color Tint { get; set; } = Color.White;
    public UiThickness Border { get; set; }
}

public sealed class UiButton : UiElement
{
    private static readonly Dictionary<Entity, List<WeakReference<UiButton>>> NativeBindings = [];
    private static readonly List<Entity> DispatchKeys = [];
    private static readonly List<UiButton> DispatchTargets = [];
    private static bool s_dispatching;
    private static double s_lastDispatchElapsed = double.NaN;

    private Action? _clicked;

    public UiButton()
    {
    }

    internal UiButton(Entity entity)
    {
        Entity = entity;
        Interactable = true;
    }

    public Entity Entity { get; }
    public bool NativeBound => Entity.Id != default;
    public bool IsValid => NativeBound && Entity.HasComponent<UiButtonComponent>();

    public event Action? Clicked
    {
        add
        {
            if (value is null)
                return;
            bool register = _clicked is null;
            _clicked += value;
            if (register && NativeBound)
                RegisterNativeBinding(this);
        }
        remove
        {
            _clicked -= value;
            if (_clicked is null && NativeBound)
                UnregisterNativeBinding(this);
        }
    }

    public static UiButton? FromEntity(Entity entity) =>
        entity.HasComponent<UiButtonComponent>() ? new UiButton(entity) : null;

    public void Invoke() => _clicked?.Invoke();

    internal static void DispatchNativeClicks()
    {
        if (s_dispatching || NativeBindings.Count == 0)
            return;
        double elapsed = NativeRuntime.ElapsedTime;
        if (elapsed == s_lastDispatchElapsed)
            return;
        s_lastDispatchElapsed = elapsed;

        s_dispatching = true;
        try
        {
            DispatchKeys.Clear();
            foreach (Entity entity in NativeBindings.Keys)
                DispatchKeys.Add(entity);

            foreach (Entity entity in DispatchKeys)
            {
                if (!NativeBindings.TryGetValue(entity, out List<WeakReference<UiButton>>? bindings))
                    continue;

                DispatchTargets.Clear();
                for (int index = bindings.Count - 1; index >= 0; --index)
                {
                    if (!bindings[index].TryGetTarget(out UiButton? button) || button._clicked is null)
                    {
                        bindings.RemoveAt(index);
                        continue;
                    }
                    DispatchTargets.Add(button);
                }

                if (bindings.Count == 0)
                {
                    NativeBindings.Remove(entity);
                    continue;
                }

                if (!NativeRuntime.ConsumeUiClick(entity))
                    continue;

                foreach (UiButton button in DispatchTargets)
                    button.Invoke();
            }
        }
        finally
        {
            DispatchTargets.Clear();
            DispatchKeys.Clear();
            s_dispatching = false;
        }
    }

    private static void RegisterNativeBinding(UiButton button)
    {
        if (!NativeBindings.TryGetValue(button.Entity, out List<WeakReference<UiButton>>? bindings))
        {
            bindings = [];
            NativeBindings.Add(button.Entity, bindings);
        }

        foreach (WeakReference<UiButton> binding in bindings)
            if (binding.TryGetTarget(out UiButton? existing) && ReferenceEquals(existing, button))
                return;
        bindings.Add(new WeakReference<UiButton>(button));
    }

    private static void UnregisterNativeBinding(UiButton button)
    {
        if (!NativeBindings.TryGetValue(button.Entity, out List<WeakReference<UiButton>>? bindings))
            return;
        for (int index = bindings.Count - 1; index >= 0; --index)
            if (!bindings[index].TryGetTarget(out UiButton? existing) || ReferenceEquals(existing, button))
                bindings.RemoveAt(index);
        if (bindings.Count == 0)
            NativeBindings.Remove(button.Entity);
    }
}

public sealed class RuntimeCanvas
{
    public RuntimeCanvas()
    {
        Root.AnchorMaximum = Vector2.One;
    }

    public RuntimeCanvas(Entity entity) : this()
    {
        Entity = entity;
    }

    public Entity Entity { get; }
    public bool NativeBound => Entity.Id != default;

    public bool SetText(Entity textEntity, string text) => NativeRuntime.SetUiText(textEntity, text);
    public bool WasClicked(Entity buttonEntity) => NativeRuntime.ConsumeUiClick(buttonEntity);

    public UiPanel Root { get; } = new() { Name = "Root" };
    public UiScaleMode ScaleMode { get; set; } = UiScaleMode.ScaleWithScreen;
    public Vector2 ReferenceResolution { get; set; } = new(1920.0f, 1080.0f);
    public float MatchWidthOrHeight { get; set; } = 0.5f;
    public UiThickness SafeArea { get; set; }

    public void Layout(Vector2 viewport)
    {
        if (viewport.X <= 0.0f || viewport.Y <= 0.0f)
            throw new ArgumentOutOfRangeException(nameof(viewport));
        float scale = ScaleMode == UiScaleMode.ScaleWithScreen
            ? MathF.Pow(viewport.X / MathF.Max(1.0f, ReferenceResolution.X), 1.0f - MatchWidthOrHeight) *
              MathF.Pow(viewport.Y / MathF.Max(1.0f, ReferenceResolution.Y), MatchWidthOrHeight)
            : 1.0f;
        var rootRect = new UiRect(
            SafeArea.Left,
            SafeArea.Top,
            viewport.X - SafeArea.Left - SafeArea.Right,
            viewport.Y - SafeArea.Top - SafeArea.Bottom);
        LayoutElement(Root, rootRect, scale);
    }

    private static void LayoutElement(UiElement element, UiRect parent, float scale)
    {
        float minimumX = parent.X + (parent.Width * element.AnchorMinimum.X);
        float minimumY = parent.Y + (parent.Height * element.AnchorMinimum.Y);
        float maximumX = parent.X + (parent.Width * element.AnchorMaximum.X);
        float maximumY = parent.Y + (parent.Height * element.AnchorMaximum.Y);
        float anchoredWidth = MathF.Max(0.0f, maximumX - minimumX);
        float anchoredHeight = MathF.Max(0.0f, maximumY - minimumY);
        float width = anchoredWidth > 0.0f ? anchoredWidth + (element.Size.X * scale) : element.Size.X * scale;
        float height = anchoredHeight > 0.0f ? anchoredHeight + (element.Size.Y * scale) : element.Size.Y * scale;
        float x = minimumX + (element.Position.X * scale) - (width * element.Pivot.X) +
                  (element.Margin.Left * scale);
        float y = minimumY + (element.Position.Y * scale) - (height * element.Pivot.Y) +
                  (element.Margin.Top * scale);
        element.LayoutRect = new UiRect(
            x,
            y,
            MathF.Max(0.0f, width - ((element.Margin.Left + element.Margin.Right) * scale)),
            MathF.Max(0.0f, height - ((element.Margin.Top + element.Margin.Bottom) * scale)));
        foreach (UiElement child in element.Children)
            LayoutElement(child, element.LayoutRect, scale);
    }
}

public static class RuntimeUi
{
    public static bool SetText(Entity entity, string text) => NativeRuntime.SetUiText(entity, text);
    public static bool WasClicked(Entity entity) => NativeRuntime.ConsumeUiClick(entity);
    public static UiButton? GetButton(Entity entity) => UiButton.FromEntity(entity);
}
