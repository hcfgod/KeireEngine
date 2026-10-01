using Keire.Editor;
using Keire.UI;
using ScriptingExamples;

namespace ScriptingEditorExamples;

[EditorExtensionId("d9918070-7683-4104-90eb-5a9bd8c5f202")]
[CustomEditor(typeof(MovementSettings))]
[CanEditMultipleObjects]
public sealed class MovementSettingsEditor : Editor
{
    public override VisualElement CreateInspectorGUI()
    {
        VisualElement root = new();
        root.Add(new Label("Shared movement tuning"));
        foreach (SerializedProperty property in SerializedObject.Properties)
            root.Add(new PropertyField(property));
        root.Add(new Button(() => SetDefaultSpeed()) { Text = "Set speed to 4" });
        return root;
    }

    private void SetDefaultSpeed()
    {
        Lifetime.ThrowIfInvalid();
        SerializedObject.Update();
        if (SerializedObject.FindProperty(nameof(MovementSettings.Speed)) is { } speed)
        {
            speed.BoxedValue = 4.0f;
            SerializedObject.ApplyModifiedProperties("Set movement speed");
        }
    }
}
