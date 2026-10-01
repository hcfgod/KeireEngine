using System.Text;
using Keire.Editor;

namespace ScriptingEditorExamples;

[EditorExtensionId("d9918070-7683-4104-90eb-5a9bd8c5f204")]
public sealed class BuildStamp : IPostprocessBuild
{
    public int Order => 100;

    public void OnPostprocessBuild(BuildContext context)
    {
        context.CancellationToken.ThrowIfCancellationRequested();
        string stamp = $"{context.Description.ProductName}\n{context.Description.Version}\n";
        context.WriteStagedFile("Metadata/game-version.txt", Encoding.UTF8.GetBytes(stamp));
    }
}
