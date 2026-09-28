using System.ComponentModel;

internal static class UiToolkitTests
{
    public static void Run()
    {
        var root = new Keire.UI.VisualElement { Name = "root" };
        var panel = new Keire.UI.VisualElement { Name = "panel" };
        panel.AddToClassList("settings-panel");
        var button = new Keire.UI.Button { Name = "apply", Text = "Apply" };
        panel.Add(button);
        root.Add(panel);

        Check(ReferenceEquals(root.Q<Keire.UI.Button>("apply"), button) &&
                  ReferenceEquals(root.Query<Keire.UI.VisualElement>(className: "settings-panel").FirstOrDefault(),
                                  panel),
              "Visual-tree queries must match names, classes, and element types.");
        CheckThrows<InvalidOperationException>(() => panel.Add(root),
            "Visual trees must reject hierarchy cycles without changing ownership.");
        Check(ReferenceEquals(panel.Parent, root) && ReferenceEquals(button.Parent, panel),
              "Rejected hierarchy mutations must preserve the previous tree.");

        var reorder = new Keire.UI.VisualElement();
        var first = new Keire.UI.Label("first");
        var second = new Keire.UI.Label("second");
        reorder.Add(first);
        reorder.Add(second);
        reorder.Add(first);
        Check(reorder.Children.SequenceEqual(new Keire.UI.VisualElement[] { second, first }),
              "Adding an existing child must move it to the end without invalidating its parent.");
        reorder.Insert(0, first);
        Check(ReferenceEquals(reorder.Children[0], first) && ReferenceEquals(first.Parent, reorder),
              "Moving a child earlier must preserve tree ownership.");

        var propagation = new List<string>();
        root.RegisterCallback<Keire.UI.ClickEvent>(_ => propagation.Add("root-trickle"),
                                                   Keire.UI.TrickleDown.TrickleDown);
        panel.RegisterCallback<Keire.UI.ClickEvent>(_ => propagation.Add("panel-trickle"),
                                                    Keire.UI.TrickleDown.TrickleDown);
        button.RegisterCallback<Keire.UI.ClickEvent>(_ => propagation.Add("target"));
        panel.RegisterCallback<Keire.UI.ClickEvent>(_ => propagation.Add("panel-bubble"));
        root.RegisterCallback<Keire.UI.ClickEvent>(_ => propagation.Add("root-bubble"));
        button.Click();
        Check(propagation.SequenceEqual(["root-trickle", "panel-trickle", "target", "panel-bubble", "root-bubble"]),
              "UI events must traverse trickle, target, and bubble phases deterministically.");

        button.Focus();
        Check(button.HasFocus, "Focusable UI controls must acquire retained-tree focus.");
        button.CapturePointer(7);
        Check(button.HasPointerCapture(7) && ReferenceEquals(root.GetCapturingElement(7), button),
              "Pointer capture must be owned by the visual-tree root.");
        panel.Remove(button);
        Check(!button.HasFocus && root.GetCapturingElement(7) is null,
              "Removing a subtree must release its focus and pointer captures.");
        panel.Add(button);

        var source = new BindingProbe { Caption = "Initial", Enabled = true };
        root.DataSource = source;
        var label = new Keire.UI.Label();
        root.Add(label);
        label.SetBinding(nameof(Keire.UI.Label.Text),
                         new Keire.UI.DataBinding { SourcePath = nameof(source.Caption) });
        Check(label.Text == "Initial", "A newly attached one-way binding must read inherited panel data.");
        source.Caption = "Updated";
        Check(label.Text == "Updated", "INotifyPropertyChanged must refresh descendant bindings.");

        label.SetBinding(nameof(Keire.UI.Label.Text),
                         new Keire.UI.DataBinding { SourcePath = "Missing.Caption" });
        Check(label.Text == "Updated" && label.LastBindingDiagnostic is
              {
                  TargetProperty: nameof(Keire.UI.Label.Text),
                  SourcePath: "Missing.Caption",
              },
              "A failed binding must retain the previous value and publish the exact source and target path.");

        var toggle = new Keire.UI.Toggle { BindingPath = nameof(source.Enabled) };
        root.Add(toggle);
        toggle.SetBinding(nameof(Keire.UI.Toggle.Value), new Keire.UI.DataBinding
        {
            SourcePath = nameof(source.Enabled),
            Mode = Keire.UI.BindingMode.TwoWay,
        });
        Check(toggle.Value && toggle.BindingPath == nameof(source.Enabled),
              "Bindable controls must preserve their authoring binding path and initialize from their source.");
        toggle.Value = false;
        Check(!source.Enabled, "Two-way bindings must write changed control values back to their source.");

        var list = new Keire.UI.ListView { ItemsSource = Enumerable.Range(0, 100_000).Cast<object?>().ToArray() };
        list.SetViewport(500, 20);
        Check(list.RealizedItems.Count == 24 && list.Children.Count == 24,
              "Virtualized lists must realize only visible items plus bounded overscan.");

        list.SetViewport(int.MaxValue, int.MaxValue);
        Check(list.RealizedItems.Count == 0, "Viewport arithmetic must not overflow into unrelated list items.");
        list.Overscan = -10;
        list.SetViewport(0, 2);
        Check(list.RealizedItems.Count == 2, "Negative overscan must not hide visible list items.");

        var field = new Keire.UI.TextField { MaxLength = 3 };
        field.Value = null!;
        Check(field.Value == string.Empty, "Length-limited text fields must normalize null safely.");
        field.Value = "abcdef";
        Check(field.Value == "abc", "Text fields must still enforce their length limit.");
        var slider = new Keire.UI.Slider { LowValue = 0, HighValue = 10, Step = 6 };
        slider.Value = 10;
        Check(slider.Value == 10, "Step snapping must remain within the slider range.");
        slider.LowValue = 10;
        slider.HighValue = 0;
        slider.Value = 0;
        Check(slider.Value == 0, "Reversed slider ranges must also constrain snapped values.");

        Keire.UI.UxmlElementDescriptor descriptor = Keire.UI.UxmlElementRegistry.Register<StatusBadge>();
        Check(descriptor.Name == "StatusBadge" &&
                  descriptor.Attributes.Any(value => value.Name == "status-text" &&
                                                     value.Property == nameof(StatusBadge.StatusText)) &&
                  descriptor.Attributes.Any(value => value.Property == nameof(Keire.UI.VisualElement.Name)),
              "Explicit custom-control registration must publish stable element and attribute metadata.");
        Check(Keire.UI.UxmlElementRegistry.Create("StatusBadge") is StatusBadge &&
                  Keire.UI.UxmlElementRegistry.Snapshot().Any(value => value.ElementType == typeof(StatusBadge)),
              "Registered custom controls must be constructible without ambient assembly scanning.");

        var nativeDocument = new Keire.UI.UIDocument(new Keire.Entity(1, new Keire.EntityId(2, 3)));
        CheckThrows<ArgumentException>(() => nativeDocument.SetBindingValue(" ", 5.0f),
            "Native document bindings must reject empty paths before crossing the native boundary.");
        CheckThrows<ArgumentOutOfRangeException>(() => nativeDocument.SetBindingValue("Player.Health", float.NaN),
            "Native document bindings must reject non-finite numbers.");
        Check(!nativeDocument.TryGetBindingValue("Player.Health", out float _) &&
                  !nativeDocument.ClearBindingSource(),
              "Native document binding handles must become inert when no presentation is active.");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private static void CheckThrows<TException>(Action action, string message) where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }
        throw new InvalidOperationException(message);
    }

    private sealed class BindingProbe : INotifyPropertyChanged
    {
        private string _caption = string.Empty;
        private bool _enabled;

        public event PropertyChangedEventHandler? PropertyChanged;

        public string Caption
        {
            get => _caption;
            set
            {
                if (_caption == value)
                    return;
                _caption = value;
                PropertyChanged?.Invoke(this, new(nameof(Caption)));
            }
        }

        public bool Enabled
        {
            get => _enabled;
            set
            {
                if (_enabled == value)
                    return;
                _enabled = value;
                PropertyChanged?.Invoke(this, new(nameof(Enabled)));
            }
        }
    }

    [Keire.UI.UxmlElement]
    private sealed class StatusBadge : Keire.UI.VisualElement
    {
        [Keire.UI.UxmlAttribute("status-text")]
        public string StatusText { get; set; } = string.Empty;
    }
}
