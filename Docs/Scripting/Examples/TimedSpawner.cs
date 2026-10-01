using System.Collections;
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f004")]
public sealed class TimedSpawner : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f104")]
    private Prefab? _prefab = null;

    protected override void OnEnable() => Begin();
    protected override void OnAfterReload()
    {
        if (Enabled && Entity.ActiveInHierarchy)
            Begin();
    }

    private void Begin()
    {
        StopAllCoroutines();
        if (_prefab is { IsValid: true })
            StartCoroutine(Spawn());
    }

    private IEnumerator Spawn()
    {
        for (int index = 0; index < 3; ++index)
        {
            if (_prefab is not { IsValid: true })
                yield break;
            Entity instance = _prefab.Instantiate(Transform.Position, Transform.Rotation);
            instance.Destroy(5.0f);
            yield return new WaitForSeconds(1.0f);
        }
    }
}
