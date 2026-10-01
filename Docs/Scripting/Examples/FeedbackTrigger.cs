using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f005")]
public sealed class FeedbackTrigger : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f105")]
    private AudioClip? _clip = null;

    protected override void OnTriggerEnter(CollisionContact contact)
    {
        if (!contact.Other.IsValid)
            return;
        if (_clip is { IsValid: true } && GetComponent<AudioSource>() is { IsValid: true } source)
            source.Play(_clip);
        if (GetComponent<Animator>() is { IsValid: true } animator)
            animator.SetTrigger("Activate");
        if (GetComponent<VfxEmitter>() is { IsValid: true } emitter)
            emitter.SendEvent("Burst", spawnCount: 8);
    }
}
