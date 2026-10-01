using System.Collections;
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f008")]
[RequireComponent(typeof(AudioSource))]
public sealed class ResidentAudio : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f108")]
    private AudioClip? _clip = null;

    private AssetLoadOperation<AudioClip>? _lease;

    protected override void OnEnable() => Begin();
    protected override void OnAfterReload()
    {
        if (Enabled && Entity.ActiveInHierarchy)
            Begin();
    }

    private void Begin()
    {
        Release();
        if (_clip is not { IsValid: true })
            return;
        _lease = Assets.LoadRuntime(_clip, AssetLoadPriority.High);
        StartCoroutine(PlayWhenReady(_lease));
    }

    private IEnumerator PlayWhenReady(AssetLoadOperation<AudioClip> lease)
    {
        yield return lease;
        if (lease.IsReady && GetComponent<AudioSource>() is { IsValid: true } source)
            source.Play(lease.Asset);
        else
            Debug.Warn($"Audio residency failed: {lease.Diagnostic.Message}");
    }

    protected override void OnDisable() => Release();
    protected override void OnBeforeReload() => Release();

    private void Release()
    {
        StopAllCoroutines();
        if (Entity.IsValid && GetComponent<AudioSource>() is { IsValid: true } source)
            source.Stop();
        _lease?.Dispose();
        _lease = null;
    }
}
