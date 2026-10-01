using Keire.Editor;

namespace ScriptingEditorExamples;

[EditorExtensionId("d9918070-7683-4104-90eb-5a9bd8c5f203")]
[ScriptedImporter(1, "gamenotes")]
public sealed class NotesImporter : ScriptedImporter
{
    public override void OnImportAsset(AssetImportContext context)
    {
        context.CancellationToken.ThrowIfCancellationRequested();
        string text = context.ReadSourceText(context.AssetPath);
        context.AddObject("notes", new TextImportArtifact(text) { Name = "Game notes" });
        context.SetMainObject("notes");
    }
}
