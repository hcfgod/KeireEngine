namespace Keire.UI;

[UxmlElement]
public class TextElement : VisualElement
{
    [UxmlAttribute] public string Text { get; set; } = string.Empty;
}

[UxmlElement]
public sealed class Label : TextElement
{
    public Label() { }
    public Label(string text) => Text = text;
}

[UxmlElement]
public class Image : VisualElement
{
    [UxmlAttribute] public Keire.Texture? Source { get; set; }
    [UxmlAttribute] public Keire.Color Tint { get; set; } = Keire.Color.White;
}

[UxmlElement]
public class Button : TextElement
{
    public Button() => Focusable = true;
    public Button(Action clicked) : this() => Clicked += clicked;
    public event Action? Clicked;

    public void Click()
    {
        var evt = new ClickEvent();
        SendEvent(evt);
        if (!evt.IsDefaultPrevented && EnabledInHierarchy)
            Clicked?.Invoke();
    }
}

[UxmlElement]
public class BindableElement : VisualElement
{
    [UxmlAttribute("binding-path")] public string BindingPath { get; set; } = string.Empty;
}

public abstract class BaseField<T> : BindableElement
{
    private T _value = default!;

    protected BaseField() => Focusable = true;

    public T Value
    {
        get => _value;
        set => SetValue(value, notify: true);
    }

    public void SetValueWithoutNotify(T value) => SetValue(value, notify: false);

    protected virtual T Normalize(T value) => value;

    private void SetValue(T value, bool notify)
    {
        value = Normalize(value);
        if (EqualityComparer<T>.Default.Equals(_value, value))
            return;
        T previous = _value;
        _value = value;
        WriteBackBinding(nameof(Value), value);
        if (notify)
            SendEvent(new ChangeEvent<T> { PreviousValue = previous, NewValue = value });
    }
}

[UxmlElement]
public sealed class TextField : BaseField<string>
{
    [UxmlAttribute] public bool Multiline { get; set; }
    [UxmlAttribute] public bool IsPasswordField { get; set; }
    [UxmlAttribute] public int MaxLength { get; set; }
    protected override string Normalize(string value) =>
        MaxLength > 0 && value.Length > MaxLength ? value[..MaxLength] : value ?? string.Empty;
}

[UxmlElement]
public class Toggle : BaseField<bool>
{
    [UxmlAttribute] public string Label { get; set; } = string.Empty;
}

[UxmlElement]
public sealed class Slider : BaseField<float>
{
    [UxmlAttribute] public float LowValue { get; set; }
    [UxmlAttribute] public float HighValue { get; set; } = 100.0f;
    [UxmlAttribute] public float Step { get; set; }

    protected override float Normalize(float value)
    {
        float result = Math.Clamp(value, Math.Min(LowValue, HighValue), Math.Max(LowValue, HighValue));
        if (Step > 0.0f)
            result = LowValue + MathF.Round((result - LowValue) / Step) * Step;
        return result;
    }
}

[UxmlElement]
public sealed class ProgressBar : VisualElement
{
    private float _value;
    [UxmlAttribute] public float LowValue { get; set; }
    [UxmlAttribute] public float HighValue { get; set; } = 100.0f;
    [UxmlAttribute] public string Title { get; set; } = string.Empty;
    public float Value
    {
        get => _value;
        set => _value = Math.Clamp(value, Math.Min(LowValue, HighValue), Math.Max(LowValue, HighValue));
    }
}

[UxmlElement]
public class ScrollView : VisualElement
{
    public Keire.Vector2 ScrollOffset { get; set; }
}

public interface ICollectionVirtualizationController
{
    int FirstVisibleIndex { get; }
    int VisibleCount { get; }
    void SetViewport(int firstVisibleIndex, int visibleCount);
}

[UxmlElement]
public class ListView : ScrollView, ICollectionVirtualizationController
{
    private readonly List<VisualElement> _realized = [];
    private IReadOnlyList<object?> _itemsSource = [];
    private int _firstVisible;
    private int _visibleCount;

    public IReadOnlyList<object?> ItemsSource
    {
        get => _itemsSource;
        set
        {
            _itemsSource = value ?? [];
            Rebuild();
        }
    }

    public Func<VisualElement> MakeItem { get; set; } = static () => new Label();
    public Action<VisualElement, int>? BindItem { get; set; }
    public int Overscan { get; set; } = 2;
    public int FirstVisibleIndex => _firstVisible;
    public int VisibleCount => _visibleCount;
    public IReadOnlyList<VisualElement> RealizedItems => _realized;

    public void SetViewport(int firstVisibleIndex, int visibleCount)
    {
        if (firstVisibleIndex < 0 || visibleCount < 0)
            throw new ArgumentOutOfRangeException(nameof(firstVisibleIndex));
        _firstVisible = firstVisibleIndex;
        _visibleCount = visibleCount;
        Rebuild();
    }

    public void RefreshItems() => Rebuild();

    private void Rebuild()
    {
        Clear();
        _realized.Clear();
        int start = Math.Clamp(_firstVisible - Overscan, 0, _itemsSource.Count);
        int end = Math.Clamp(_firstVisible + _visibleCount + Overscan, start, _itemsSource.Count);
        for (int index = start; index < end; ++index)
        {
            VisualElement item = MakeItem() ?? throw new InvalidOperationException("ListView.MakeItem returned null.");
            item.UserData = _itemsSource[index];
            BindItem?.Invoke(item, index);
            Add(item);
            _realized.Add(item);
        }
    }
}

[UxmlElement]
public sealed class TreeView : ListView;

[UxmlElement]
public sealed class DropdownField : BaseField<string>
{
    public IReadOnlyList<string> Choices { get; set; } = [];
    protected override string Normalize(string value) =>
        Choices.Count == 0 || Choices.Contains(value, StringComparer.Ordinal) ? value : Choices[0];
}

[UxmlElement]
public sealed class Foldout : Toggle
{
    [UxmlAttribute] public string Text { get; set; } = string.Empty;
}

[UxmlElement]
public sealed class TabView : VisualElement
{
    private int _selectedIndex = -1;
    public int SelectedIndex
    {
        get => _selectedIndex;
        set => _selectedIndex = Children.Count == 0 ? -1 : Math.Clamp(value, 0, Children.Count - 1);
    }
}

[UxmlElement]
public sealed class Toolbar : VisualElement;

[UxmlElement]
public sealed class TemplateContainer : VisualElement
{
    [UxmlAttribute] public string TemplateName { get; set; } = string.Empty;
}
