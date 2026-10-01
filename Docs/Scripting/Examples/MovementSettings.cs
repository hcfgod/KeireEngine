using Keire;

namespace ScriptingExamples;

[StableAssetTypeId("d9918070-7683-4104-90eb-5a9bd8c5f012")]
[CreateAssetMenu("Examples/Movement Settings", "MovementSettings")]
public sealed class MovementSettings : ScriptableObject
{
    [StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f113"), Min(0.0)]
    public float Speed = 4.0f;

    [StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f114")]
    public AudioClip? Footstep;
}
