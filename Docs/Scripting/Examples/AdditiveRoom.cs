using System.Collections;
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f007")]
public sealed class AdditiveRoom : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f107")]
    private SceneAsset? _room = null;

    private SceneLoadOperation? _operation;
    private Scene? _loaded;

    public void Load()
    {
        if (_operation is not null || _loaded is { IsLoaded: true } || _room is not { IsValid: true })
            return;
        _operation = SceneManager.LoadSceneAsync(_room, SceneLoadMode.Additive);
        StartCoroutine(Observe(_operation));
    }

    private IEnumerator Observe(SceneLoadOperation operation)
    {
        yield return operation;
        if (operation.Succeeded)
            _loaded = operation.Scene;
        else
            Debug.Warn($"Room load ended with {operation.State}: {operation.Error}");
        _operation = null;
    }

    protected override void Update()
    {
        if (Input.Keyboard.Current?.lKey.WasPressedThisFrame == true)
            Load();
    }

    protected override void OnDisable() => Release();
    protected override void OnBeforeReload() => Release();

    private void Release()
    {
        StopAllCoroutines();
        if (_operation is { Succeeded: true })
            _loaded = _operation.Scene;
        else
            _operation?.Cancel();
        _operation = null;
        if (_loaded is { IsLoaded: true } && !SceneManager.UnloadScene(_loaded))
            Debug.Warn("The owned room could not be unloaded.");
        _loaded = null;
    }
}
