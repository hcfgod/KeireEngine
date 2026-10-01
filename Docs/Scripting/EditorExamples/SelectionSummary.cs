using Keire.Editor;
using Keire.UI;

namespace ScriptingEditorExamples;

[EditorExtensionId("d9918070-7683-4104-90eb-5a9bd8c5f201")]
[EditorWindow("Selection Summary")]
public sealed class SelectionSummary : EditorWindow
{
    private readonly Label _summary = new();

    protected override void OnEnable() => Selection.Changed += Refresh;
    protected override void OnDisable() => Selection.Changed -= Refresh;

    public override void CreateGUI()
    {
        RootVisualElement.Clear();
        RootVisualElement.Add(_summary);
        Refresh();
    }

    private void Refresh() => _summary.Text = $"Selected objects: {Selection.Objects.Count}";
}
