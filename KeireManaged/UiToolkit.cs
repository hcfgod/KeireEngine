using System.ComponentModel;
using System.Reflection;

namespace Keire.UI;

[Keire.StableAssetTypeId("4b454952-4555-4954-5245-450000000001")]
public sealed class VisualTreeAsset : Keire.Asset;

[Keire.StableAssetTypeId("4b454952-4555-4953-5459-4c4500000001")]
public sealed class StyleSheet : Keire.Asset;

[Keire.StableAssetTypeId("4b454952-4555-4950-414e-454c00000001")]
public sealed class PanelSettings : Keire.Asset;

[Keire.StableComponentId("4b454952-4555-4944-4f43-554d454e5401")]
public sealed class UIDocument : Keire.Component
{
    internal UIDocument(Keire.Entity entity) : base(entity) { }

    public VisualTreeAsset? VisualTreeAsset
    {
        get => GetBuiltinAsset<VisualTreeAsset>("visualTree");
        set => SetBuiltinAsset("visualTree", value);
    }

    public PanelSettings? PanelSettings
    {
        get => GetBuiltinAsset<PanelSettings>("panelSettings");
        set => SetBuiltinAsset("panelSettings", value);
    }

    public int SortingOrder
    {
        get => checked((int)GetBuiltinInteger("sortingOrder"));
        set => SetBuiltinInteger("sortingOrder", value);
    }

    public bool ReceivesInput
    {
        get => GetBuiltinBoolean("receivesInput");
        set => SetBuiltinBoolean("receivesInput", value);
    }

    /// <summary>Returns the live retained root owned by this scene document, or null until it is presented.</summary>
    public RuntimeVisualElement? RootVisualElement => RuntimeVisualElement.ResolveRoot(Entity);

    /// <summary>Finds the first live source-backed element with the supplied authored name.</summary>
    public RuntimeVisualElement? Q(string name) => RuntimeVisualElement.Resolve(Entity, name);

    /// <summary>Finds the live source-backed element with the supplied stable asset identifier.</summary>
    public RuntimeVisualElement? Q(Keire.AssetId stableId) => RuntimeVisualElement.Resolve(Entity, stableId);
}

public enum RuntimeVisualElementType : byte
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

/// <summary>
/// A generation-checked handle to an element in a scene UIDocument's native retained tree. Handles become inert after
/// a successful document reload or destruction; a failed reload keeps the last-good generation alive.
/// </summary>
public sealed class RuntimeVisualElement
{
    private readonly Keire.Entity _document;
    private readonly Keire.NativeUiDocumentElement _element;

    private RuntimeVisualElement(Keire.Entity document, Keire.NativeUiDocumentElement element) =>
        (_document, _element) = (document, element);

    public Keire.AssetId StableId => _element.StableId;
    public RuntimeVisualElementType Type => (RuntimeVisualElementType)_element.Type;
    public bool IsAlive => Keire.NativeRuntimeUi.DocumentElementAlive(_document, _element);

    public string Text
    {
        get => Keire.NativeRuntimeUi.GetDocumentElementText(_document, _element);
        set => Keire.NativeRuntimeUi.SetDocumentElementText(_document, _element, value);
    }

    public float Value
    {
        get => Keire.NativeRuntimeUi.GetDocumentElementValue(_document, _element);
        set => Keire.NativeRuntimeUi.SetDocumentElementValue(_document, _element, value);
    }

    public bool Checked
    {
        get => GetFlag(Keire.NativeUiDocumentFlag.Checked);
        set => SetFlag(Keire.NativeUiDocumentFlag.Checked, value);
    }

    public bool Interactable
    {
        get => GetFlag(Keire.NativeUiDocumentFlag.Interactable);
        set => SetFlag(Keire.NativeUiDocumentFlag.Interactable, value);
    }

    public bool Enabled
    {
        get => GetFlag(Keire.NativeUiDocumentFlag.Enabled);
        set => SetFlag(Keire.NativeUiDocumentFlag.Enabled, value);
    }

    public bool HasFocus => GetFlag(Keire.NativeUiDocumentFlag.Focused);
    public bool ClickedThisFrame => ConsumeEvent(Keire.NativeUiEventType.Click);
    public bool ChangedThisFrame => ConsumeEvent(Keire.NativeUiEventType.ValueChanged) ||
                                    ConsumeEvent(Keire.NativeUiEventType.TextChanged);
    public bool SubmittedThisFrame => ConsumeEvent(Keire.NativeUiEventType.Submit);
    public bool CancelledThisFrame => ConsumeEvent(Keire.NativeUiEventType.Cancel);

    public void Focus() => Keire.NativeRuntimeUi.FocusDocumentElement(_document, _element);

    internal static RuntimeVisualElement? ResolveRoot(Keire.Entity document)
    {
        Keire.NativeUiDocumentElement? element = Keire.NativeRuntimeUi.ResolveDocumentRoot(document);
        return element is { } value ? new RuntimeVisualElement(document, value) : null;
    }

    internal static RuntimeVisualElement? Resolve(Keire.Entity document, string name)
    {
        Keire.NativeUiDocumentElement? element = Keire.NativeRuntimeUi.ResolveDocumentElement(document, name);
        return element is { } value ? new RuntimeVisualElement(document, value) : null;
    }

    internal static RuntimeVisualElement? Resolve(Keire.Entity document, Keire.AssetId stableId)
    {
        Keire.NativeUiDocumentElement? element = Keire.NativeRuntimeUi.ResolveDocumentElement(document, stableId);
        return element is { } value ? new RuntimeVisualElement(document, value) : null;
    }

    private bool GetFlag(Keire.NativeUiDocumentFlag property) =>
        Keire.NativeRuntimeUi.GetDocumentElementFlag(_document, _element, property);

    private void SetFlag(Keire.NativeUiDocumentFlag property, bool value) =>
        Keire.NativeRuntimeUi.SetDocumentElementFlag(_document, _element, property, value);

    private bool ConsumeEvent(Keire.NativeUiEventType type)
    {
        if (!IsAlive)
            throw new InvalidOperationException(
                "The UI Document element is unavailable because its document was destroyed or reloaded.");
        return Keire.NativeRuntimeUi.ConsumeDocumentElementEvent(_document, _element, type);
    }
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class UxmlElementAttribute : Attribute
{
    public UxmlElementAttribute(string name = "") => Name = name;
    public string Name { get; }
}

[AttributeUsage(AttributeTargets.Property, Inherited = true)]
public sealed class UxmlAttributeAttribute : Attribute
{
    public UxmlAttributeAttribute(string name = "") => Name = name;
    public string Name { get; }
}

public sealed record UxmlAttributeDescriptor(string Name, string Property, Type ValueType);

public sealed record UxmlElementDescriptor(string Name, Type ElementType,
                                            IReadOnlyList<UxmlAttributeDescriptor> Attributes,
                                            long Generation);

public static class UxmlElementRegistry
{
    private static readonly object Gate = new();
    private static readonly Dictionary<string, UxmlElementDescriptor> Elements = new(StringComparer.Ordinal);
    private static long _generation;

    public static long Generation
    {
        get
        {
            lock (Gate)
                return _generation;
        }
    }

    public static UxmlElementDescriptor Register<T>() where T : VisualElement, new()
    {
        Type type = typeof(T);
        UxmlElementAttribute marker = type.GetCustomAttribute<UxmlElementAttribute>(inherit: false) ??
            throw new InvalidOperationException($"Custom UI element '{type.FullName}' requires [UxmlElement].");
        string name = string.IsNullOrWhiteSpace(marker.Name) ? type.Name : marker.Name;
        ValidateUxmlName(name);
        var attributes = type.GetProperties(BindingFlags.Instance | BindingFlags.Public)
            .Select(property => (Property: property,
                                 Marker: property.GetCustomAttribute<UxmlAttributeAttribute>(inherit: true)))
            .Where(value => value.Marker is not null)
            .Select(value =>
            {
                if (!value.Property.CanRead || !value.Property.CanWrite || value.Property.GetIndexParameters().Length != 0)
                    throw new InvalidOperationException(
                        $"UXML attribute property '{type.FullName}.{value.Property.Name}' must be readable and writable.");
                string attributeName = string.IsNullOrWhiteSpace(value.Marker!.Name)
                    ? value.Property.Name
                    : value.Marker.Name;
                ValidateUxmlName(attributeName);
                return new UxmlAttributeDescriptor(attributeName, value.Property.Name, value.Property.PropertyType);
            })
            .OrderBy(value => value.Name, StringComparer.Ordinal)
            .ToArray();
        if (attributes.Select(value => value.Name).Distinct(StringComparer.Ordinal).Count() != attributes.Length)
            throw new InvalidOperationException($"Custom UI element '{type.FullName}' contains duplicate UXML attributes.");

        lock (Gate)
        {
            if (Elements.TryGetValue(name, out UxmlElementDescriptor? current) && current.ElementType != type)
                throw new InvalidOperationException(
                    $"UXML element name '{name}' is already registered by '{current.ElementType.FullName}'.");
            long generation = ++_generation;
            var descriptor = new UxmlElementDescriptor(name, type, attributes, generation);
            Elements[name] = descriptor;
            return descriptor;
        }
    }

    public static VisualElement Create(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        Type type;
        lock (Gate)
        {
            if (!Elements.TryGetValue(name, out UxmlElementDescriptor? descriptor))
                throw new KeyNotFoundException($"UXML element '{name}' is not registered.");
            type = descriptor.ElementType;
        }
        return (VisualElement)(Activator.CreateInstance(type) ??
            throw new InvalidOperationException($"UXML element '{name}' could not be constructed."));
    }

    public static IReadOnlyList<UxmlElementDescriptor> Snapshot()
    {
        lock (Gate)
            return Elements.Values.OrderBy(value => value.Name, StringComparer.Ordinal).ToArray();
    }

    private static void ValidateUxmlName(string name)
    {
        if (string.IsNullOrWhiteSpace(name) || (!char.IsLetter(name[0]) && name[0] != '_') ||
            name.Any(character => !char.IsLetterOrDigit(character) && character is not '_' and not '-'))
            throw new InvalidOperationException($"UXML name '{name}' is not a stable identifier.");
    }
}

public enum TrickleDown : byte { NoTrickleDown, TrickleDown }
public enum PropagationPhase : byte { None, TrickleDown, AtTarget, BubbleUp }
public enum BindingMode : byte { OneWay, TwoWay, OneTime }
public enum DisplayStyle : byte { Flex, None }
public enum Position : byte { Relative, Absolute }
public enum FlexDirection : byte { Column, ColumnReverse, Row, RowReverse }
public enum Wrap : byte { NoWrap, Wrap, WrapReverse }
public enum Justify : byte { FlexStart, Center, FlexEnd, SpaceBetween, SpaceAround, SpaceEvenly }
public enum Align : byte { Auto, FlexStart, Center, FlexEnd, Stretch }
public enum Overflow : byte { Visible, Hidden, Scroll }
public enum NavigationDirection : byte { Previous, Next, Left, Right, Up, Down }

public sealed class Style
{
    public DisplayStyle Display { get; set; } = DisplayStyle.Flex;
    public Position Position { get; set; }
    public FlexDirection FlexDirection { get; set; } = FlexDirection.Column;
    public Wrap FlexWrap { get; set; }
    public Justify JustifyContent { get; set; }
    public Align AlignItems { get; set; } = Align.Stretch;
    public Align AlignSelf { get; set; } = Align.Auto;
    public Overflow Overflow { get; set; } = Overflow.Visible;
    public float? Width { get; set; }
    public float? Height { get; set; }
    public float? MinWidth { get; set; }
    public float? MinHeight { get; set; }
    public float? MaxWidth { get; set; }
    public float? MaxHeight { get; set; }
    public float FlexGrow { get; set; }
    public float FlexShrink { get; set; } = 1.0f;
    public float Gap { get; set; }
    public float Opacity { get; set; } = 1.0f;
    public Keire.Color Color { get; set; } = Keire.Color.White;
    public Keire.Color BackgroundColor { get; set; } = new(0.0f, 0.0f, 0.0f, 0.0f);
}

public abstract class EventBase
{
    public VisualElement? Target { get; internal set; }
    public VisualElement? CurrentTarget { get; internal set; }
    public PropagationPhase PropagationPhase { get; internal set; }
    public bool IsPropagationStopped { get; private set; }
    public bool IsImmediatePropagationStopped { get; private set; }
    public bool IsDefaultPrevented { get; private set; }
    public virtual bool Bubbles => true;
    public virtual bool TricklesDown => true;

    public void StopPropagation() => IsPropagationStopped = true;

    public void StopImmediatePropagation()
    {
        IsImmediatePropagationStopped = true;
        IsPropagationStopped = true;
    }

    public void PreventDefault() => IsDefaultPrevented = true;
}

public abstract class PointerEventBase : EventBase
{
    public int PointerId { get; init; }
    public int Button { get; init; }
    public Keire.Vector2 Position { get; init; }
}

public sealed class PointerDownEvent : PointerEventBase;
public sealed class PointerUpEvent : PointerEventBase;
public sealed class PointerMoveEvent : PointerEventBase;
public sealed class ClickEvent : PointerEventBase;
public sealed class FocusInEvent : EventBase;
public sealed class FocusOutEvent : EventBase;
public sealed class SubmitEvent : EventBase;

public sealed class KeyDownEvent : EventBase
{
    public required string Key { get; init; }
    public bool ShiftKey { get; init; }
    public bool CtrlKey { get; init; }
    public bool AltKey { get; init; }
}

public sealed class TextInputEvent : EventBase
{
    public required string Text { get; init; }
}

public sealed class NavigationMoveEvent : EventBase
{
    public NavigationDirection Direction { get; init; }
}

public sealed class ChangeEvent<T> : EventBase
{
    public required T PreviousValue { get; init; }
    public required T NewValue { get; init; }
}

public sealed class DataBinding
{
    public required string SourcePath { get; init; }
    public BindingMode Mode { get; init; } = BindingMode.OneWay;
    public Func<object?, object?>? ToTarget { get; init; }
    public Func<object?, object?>? ToSource { get; init; }
}

public sealed record BindingDiagnostic(string TargetProperty, string SourcePath, Type SourceType, Type TargetType,
                                       string Message);

[UxmlElement]
public class VisualElement
{
    private sealed record Callback(Delegate Handler, bool Trickle);
    private sealed class BindingRegistration
    {
        public required string TargetProperty { get; init; }
        public required DataBinding Binding { get; init; }
        public bool Applied { get; set; }
    }

    private readonly List<VisualElement> _children = [];
    private readonly HashSet<string> _classes = new(StringComparer.Ordinal);
    private readonly Dictionary<Type, List<Callback>> _callbacks = [];
    private readonly List<BindingRegistration> _bindings = [];
    private readonly Dictionary<int, VisualElement> _pointerCaptures = [];
    private object? _dataSource;
    private VisualElement? _focusedElement;

    public VisualElement()
    {
        StableId = Guid.NewGuid();
        Style = new Style();
    }

    public Guid StableId { get; internal set; }
    [UxmlAttribute] public string Name { get; set; } = string.Empty;
    public VisualElement? Parent { get; private set; }
    public IReadOnlyList<VisualElement> Children => _children;
    public IReadOnlyCollection<string> ClassList => _classes;
    public Style Style { get; }
    public bool EnabledSelf { get; private set; } = true;
    public bool EnabledInHierarchy => EnabledSelf && (Parent?.EnabledInHierarchy ?? true);
    public bool HasFocus => ReferenceEquals(Root()._focusedElement, this);
    public bool Focusable { get; set; }
    public int TabIndex { get; set; }
    public string Tooltip { get; set; } = string.Empty;
    public object? UserData { get; set; }
    public BindingDiagnostic? LastBindingDiagnostic { get; private set; }
    public object? DataSource
    {
        get => _dataSource ?? Parent?.DataSource;
        set
        {
            if (ReferenceEquals(_dataSource, value))
                return;
            if (_dataSource is INotifyPropertyChanged previous)
                previous.PropertyChanged -= SourceChanged;
            _dataSource = value;
            if (_dataSource is INotifyPropertyChanged current)
                current.PropertyChanged += SourceChanged;
            UpdateBindingsRecursively();
        }
    }

    public void Add(VisualElement child) => Insert(_children.Count, child);

    public void Insert(int index, VisualElement child)
    {
        ArgumentNullException.ThrowIfNull(child);
        if (ReferenceEquals(child, this) || IsDescendantOf(child))
            throw new InvalidOperationException("A visual tree cannot contain a hierarchy cycle.");
        if ((uint)index > (uint)_children.Count)
            throw new ArgumentOutOfRangeException(nameof(index));
        child.RemoveFromHierarchy();
        child.Parent = this;
        _children.Insert(index, child);
        child.UpdateBindingsRecursively();
    }

    public bool Remove(VisualElement child)
    {
        ArgumentNullException.ThrowIfNull(child);
        if (!_children.Remove(child))
            return false;
        Root().ReleaseOwnedState(child);
        child.Parent = null;
        child.UpdateBindingsRecursively();
        return true;
    }

    public void RemoveFromHierarchy() => Parent?.Remove(this);

    public void Clear()
    {
        foreach (VisualElement child in _children)
        {
            Root().ReleaseOwnedState(child);
            child.Parent = null;
        }
        _children.Clear();
    }

    public void AddToClassList(string className)
    {
        ValidateClassName(className);
        _classes.Add(className);
    }

    public bool RemoveFromClassList(string className) => _classes.Remove(className);
    public bool ClassListContains(string className) => _classes.Contains(className);
    public void EnableInClassList(string className, bool enable)
    {
        if (enable)
            AddToClassList(className);
        else
            _classes.Remove(className);
    }

    public void SetEnabled(bool enabled)
    {
        if (EnabledSelf == enabled)
            return;
        EnabledSelf = enabled;
        if (!enabled)
            Root().ReleaseOwnedState(this);
    }

    public void Focus()
    {
        if (!Focusable || !EnabledInHierarchy)
            return;
        VisualElement root = Root();
        if (ReferenceEquals(root._focusedElement, this))
            return;
        VisualElement? previous = root._focusedElement;
        root._focusedElement = this;
        previous?.SendEvent(new FocusOutEvent());
        SendEvent(new FocusInEvent());
    }

    public void Blur()
    {
        VisualElement root = Root();
        if (!ReferenceEquals(root._focusedElement, this))
            return;
        root._focusedElement = null;
        SendEvent(new FocusOutEvent());
    }

    public void CapturePointer(int pointerId)
    {
        if (pointerId < 0)
            throw new ArgumentOutOfRangeException(nameof(pointerId));
        if (!EnabledInHierarchy)
            throw new InvalidOperationException("A disabled UI element cannot capture a pointer.");
        Root()._pointerCaptures[pointerId] = this;
    }

    public void ReleasePointer(int pointerId)
    {
        VisualElement root = Root();
        if (root._pointerCaptures.TryGetValue(pointerId, out VisualElement? captured) &&
            ReferenceEquals(captured, this))
            root._pointerCaptures.Remove(pointerId);
    }

    public bool HasPointerCapture(int pointerId) =>
        Root()._pointerCaptures.TryGetValue(pointerId, out VisualElement? captured) && ReferenceEquals(captured, this);

    public VisualElement? GetCapturingElement(int pointerId) =>
        Root()._pointerCaptures.GetValueOrDefault(pointerId);

    public T? Q<T>(string? name = null, string? className = null) where T : VisualElement =>
        Query<T>(name, className).FirstOrDefault();

    public UQueryBuilder<T> Query<T>(string? name = null, string? className = null) where T : VisualElement =>
        new(this, name, className);

    public void RegisterCallback<TEvent>(Action<TEvent> callback,
                                         TrickleDown trickleDown = TrickleDown.NoTrickleDown)
        where TEvent : EventBase
    {
        ArgumentNullException.ThrowIfNull(callback);
        if (!_callbacks.TryGetValue(typeof(TEvent), out List<Callback>? callbacks))
            _callbacks.Add(typeof(TEvent), callbacks = []);
        callbacks.Add(new Callback(callback, trickleDown == TrickleDown.TrickleDown));
    }

    public void UnregisterCallback<TEvent>(Action<TEvent> callback,
                                           TrickleDown trickleDown = TrickleDown.NoTrickleDown)
        where TEvent : EventBase
    {
        if (_callbacks.TryGetValue(typeof(TEvent), out List<Callback>? callbacks))
            callbacks.RemoveAll(value => value.Handler.Equals(callback) &&
                                         value.Trickle == (trickleDown == TrickleDown.TrickleDown));
    }

    public void SendEvent(EventBase evt)
    {
        ArgumentNullException.ThrowIfNull(evt);
        evt.Target ??= this;
        if (!ReferenceEquals(evt.Target, this))
            throw new InvalidOperationException("Events must be dispatched by their target element.");
        var path = new List<VisualElement>();
        for (VisualElement? current = Parent; current is not null; current = current.Parent)
            path.Add(current);
        if (evt.TricklesDown)
        {
            for (int index = path.Count - 1; index >= 0 && !evt.IsPropagationStopped; --index)
                path[index].Invoke(evt, PropagationPhase.TrickleDown, true);
        }
        if (!evt.IsPropagationStopped)
        {
            Invoke(evt, PropagationPhase.AtTarget, true);
            if (!evt.IsImmediatePropagationStopped)
                Invoke(evt, PropagationPhase.AtTarget, false);
        }
        if (evt.Bubbles)
        {
            foreach (VisualElement current in path)
            {
                if (evt.IsPropagationStopped)
                    break;
                current.Invoke(evt, PropagationPhase.BubbleUp, false);
            }
        }
        evt.CurrentTarget = null;
        evt.PropagationPhase = PropagationPhase.None;
    }

    public void SetBinding(string targetProperty, DataBinding binding)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(targetProperty);
        ArgumentNullException.ThrowIfNull(binding);
        PropertyInfo target = GetType().GetProperty(targetProperty,
            BindingFlags.Instance | BindingFlags.Public | BindingFlags.IgnoreCase) ??
            throw new ArgumentException($"Target property '{targetProperty}' does not exist.", nameof(targetProperty));
        if (!target.CanWrite)
            throw new ArgumentException($"Target property '{targetProperty}' is read-only.", nameof(targetProperty));
        _bindings.RemoveAll(value => string.Equals(value.TargetProperty, target.Name, StringComparison.Ordinal));
        _bindings.Add(new BindingRegistration { TargetProperty = target.Name, Binding = binding });
        UpdateBindings();
    }

    public bool ClearBinding(string targetProperty) =>
        _bindings.RemoveAll(value => string.Equals(value.TargetProperty, targetProperty,
                                                    StringComparison.OrdinalIgnoreCase)) != 0;

    public void UpdateBindings()
    {
        object? source = DataSource;
        if (source is null)
            return;
        foreach (BindingRegistration registration in _bindings)
        {
            if (registration.Binding.Mode == BindingMode.OneTime && registration.Applied)
                continue;
            PropertyInfo target = GetType().GetProperty(registration.TargetProperty,
                BindingFlags.Instance | BindingFlags.Public)!;
            object? previous = target.GetValue(this);
            try
            {
                object? value = ReadPath(source, registration.Binding.SourcePath);
                value = registration.Binding.ToTarget?.Invoke(value) ?? value;
                target.SetValue(this, ConvertValue(value, target.PropertyType));
                registration.Applied = true;
                LastBindingDiagnostic = null;
            }
            catch (Exception error)
            {
                try
                {
                    target.SetValue(this, previous);
                }
                catch
                {
                }
                LastBindingDiagnostic = new BindingDiagnostic(
                    target.Name, registration.Binding.SourcePath, source.GetType(), target.PropertyType,
                    $"Binding '{source.GetType().FullName}.{registration.Binding.SourcePath}' to " +
                    $"'{GetType().FullName}.{target.Name}' failed: {error.GetBaseException().Message}");
            }
        }
    }

    protected void WriteBackBinding(string targetProperty, object? value)
    {
        BindingRegistration? registration = _bindings.FirstOrDefault(binding =>
            string.Equals(binding.TargetProperty, targetProperty, StringComparison.OrdinalIgnoreCase) &&
            binding.Binding.Mode == BindingMode.TwoWay);
        object? source = DataSource;
        if (registration is null || source is null)
            return;
        value = registration.Binding.ToSource?.Invoke(value) ?? value;
        WritePath(source, registration.Binding.SourcePath, value);
    }

    internal IEnumerable<VisualElement> DescendantsAndSelf()
    {
        yield return this;
        foreach (VisualElement child in _children)
            foreach (VisualElement descendant in child.DescendantsAndSelf())
                yield return descendant;
    }

    private void Invoke(EventBase evt, PropagationPhase phase, bool trickle)
    {
        if (!_callbacks.TryGetValue(evt.GetType(), out List<Callback>? callbacks))
            return;
        evt.CurrentTarget = this;
        evt.PropagationPhase = phase;
        foreach (Callback callback in callbacks.ToArray())
        {
            if (callback.Trickle != trickle)
                continue;
            callback.Handler.DynamicInvoke(evt);
            if (evt.IsImmediatePropagationStopped)
                break;
        }
    }

    private bool IsDescendantOf(VisualElement candidate)
    {
        for (VisualElement? current = this; current is not null; current = current.Parent)
            if (ReferenceEquals(current, candidate))
                return true;
        return false;
    }

    private VisualElement Root()
    {
        VisualElement current = this;
        while (current.Parent is not null)
            current = current.Parent;
        return current;
    }

    private void ReleaseOwnedState(VisualElement subtree)
    {
        if (_focusedElement is not null && _focusedElement.IsDescendantOf(subtree))
        {
            VisualElement previous = _focusedElement;
            _focusedElement = null;
            previous.SendEvent(new FocusOutEvent());
        }
        foreach (int pointerId in _pointerCaptures
                     .Where(value => value.Value.IsDescendantOf(subtree))
                     .Select(value => value.Key)
                     .ToArray())
            _pointerCaptures.Remove(pointerId);
    }

    private void SourceChanged(object? sender, PropertyChangedEventArgs args) => UpdateBindingsRecursively();

    private void UpdateBindingsRecursively()
    {
        UpdateBindings();
        foreach (VisualElement child in _children)
            child.UpdateBindingsRecursively();
    }

    private static object? ReadPath(object source, string path)
    {
        string[] segments = path.Split('.', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (segments.Length == 0)
            throw new InvalidOperationException("A binding requires a source property path.");
        object? current = source;
        foreach (string segment in segments)
        {
            if (current is null)
                throw new InvalidOperationException($"Binding path '{path}' traverses a null value before '{segment}'.");
            PropertyInfo property = current.GetType().GetProperty(segment,
                BindingFlags.Instance | BindingFlags.Public | BindingFlags.IgnoreCase) ??
                throw new InvalidOperationException($"Binding path '{path}' has no readable property '{segment}'.");
            if (!property.CanRead || property.GetIndexParameters().Length != 0)
                throw new InvalidOperationException($"Binding path '{path}' property '{segment}' is not readable.");
            current = property.GetValue(current);
        }
        return current;
    }

    private static void WritePath(object source, string path, object? value)
    {
        string[] segments = path.Split('.', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (segments.Length == 0)
            throw new InvalidOperationException("A two-way binding requires a source property path.");
        object current = source;
        for (int index = 0; index + 1 < segments.Length; ++index)
        {
            current = current.GetType().GetProperty(segments[index],
                BindingFlags.Instance | BindingFlags.Public | BindingFlags.IgnoreCase)?.GetValue(current) ??
                throw new InvalidOperationException($"Binding path '{path}' contains a null or missing property.");
        }
        PropertyInfo property = current.GetType().GetProperty(segments[^1],
            BindingFlags.Instance | BindingFlags.Public | BindingFlags.IgnoreCase) ??
            throw new InvalidOperationException($"Binding source property '{path}' does not exist.");
        if (!property.CanWrite)
            throw new InvalidOperationException($"Binding source property '{path}' is read-only.");
        property.SetValue(current, ConvertValue(value, property.PropertyType));
    }

    private static object? ConvertValue(object? value, Type destination)
    {
        if (value is null)
            return destination.IsValueType && Nullable.GetUnderlyingType(destination) is null
                ? Activator.CreateInstance(destination)
                : null;
        Type target = Nullable.GetUnderlyingType(destination) ?? destination;
        return target.IsInstanceOfType(value) ? value : Convert.ChangeType(value, target);
    }

    private static void ValidateClassName(string className)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(className);
        if (!char.IsLetter(className[0]) && className[0] != '_')
            throw new ArgumentException("A style class must start with a letter or underscore.", nameof(className));
        if (className.Any(character => !char.IsLetterOrDigit(character) && character is not '_' and not '-'))
            throw new ArgumentException("A style class contains an unsupported character.", nameof(className));
    }
}

public readonly struct UQueryBuilder<T> where T : VisualElement
{
    private readonly VisualElement _root;
    private readonly string? _name;
    private readonly string? _className;

    internal UQueryBuilder(VisualElement root, string? name, string? className)
    {
        _root = root;
        _name = name;
        _className = className;
    }

    public IEnumerable<T> ToList() => _root.DescendantsAndSelf().OfType<T>().Where(Matches);
    public T? FirstOrDefault() => ToList().FirstOrDefault();
    public void ForEach(Action<T> action)
    {
        ArgumentNullException.ThrowIfNull(action);
        foreach (T value in ToList())
            action(value);
    }

    private bool Matches(T element) =>
        (_name is null || string.Equals(element.Name, _name, StringComparison.Ordinal)) &&
        (_className is null || element.ClassListContains(_className));
}
