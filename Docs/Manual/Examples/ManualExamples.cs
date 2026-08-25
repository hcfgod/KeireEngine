using Keire;

namespace KeireManualExamples;

[StableComponentId("7b5ac27e-4531-4a42-97b8-9a643661660e")]
public sealed class Mover : Behaviour
{
    [SerializeField, StableFieldId("4cf59f74-236e-43c2-bb37-863a0ee5500c")]
    private float _speed = 4.0f;

    [SerializeField, StableFieldId("3f88533e-d180-4f33-a77f-86cb3bbc91bc")]
    private InputActionAsset? _inputActions = null;

    private InputAction? _move;

    protected override void OnEnable()
    {
        _move = _inputActions?.FindAction("Player/Move");
        _move?.Enable();
    }

    protected override void OnDisable()
    {
        _move?.Disable();
        _move = null;
    }

    protected override void Update()
    {
        Vector2 input = _move?.ReadValue<Vector2>() ?? Vector2.Zero;
        Vector3 movement = new(input.X, 0.0f, input.Y);
        Entity.Transform.Position += movement * (_speed * Time.DeltaTime);
    }
}

[StableComponentId("38e0b2bf-d750-47d4-8505-b5a032589222")]
public sealed class SpawnPoint : Behaviour
{
    [SerializeField, StableFieldId("166228ba-f8e3-4680-85af-df3a7e910a86")]
    private Prefab? _prefab = null;

    public Entity? Spawn()
    {
        if (_prefab is not { IsValid: true })
            return null;

        return _prefab.Instantiate(Entity.Transform.Position, Entity.Transform.Rotation);
    }
}

[StableComponentId("b26374c3-54e7-41a3-ae24-24dbbc10f0e6")]
public sealed class ScenePortal : Behaviour
{
    [SerializeField, StableFieldId("eed8ed79-02a9-4d23-881c-a7751913cd08")]
    private SceneAsset? _destination = null;

    private bool _loading;

    protected override void OnTriggerEnter(CollisionContact contact)
    {
        if (_loading || _destination is not { IsValid: true })
            return;

        _loading = true;
        _ = StartCoroutine(LoadDestination());
    }

    private System.Collections.IEnumerator LoadDestination()
    {
        SceneLoadOperation operation = SceneManager.LoadSceneAsync(_destination!, SceneLoadMode.Single);
        yield return operation;

        if (!operation.Succeeded)
            Debug.Error($"Scene load failed: {operation.Error}");
        _loading = false;
    }
}

[StableAssetTypeId("c469739a-e7ac-4d9a-8e32-148fbb01baf2")]
[CreateAssetMenu("Gameplay/Movement Tuning", "MovementTuning")]
public sealed class MovementTuning : ScriptableObject
{
    [StableFieldId("4f1b9c0e-1622-45e3-90f9-a0ba891bfeab")]
    public float WalkSpeed = 4.0f;

    [StableFieldId("d60cc874-58d4-4184-9d9d-c6f0bca69657")]
    public float SprintSpeed = 7.5f;
}

[StableComponentId("ee3548b8-662a-4898-8db0-27b034d9f08a")]
public sealed class ResumeButton : Behaviour
{
    [SerializeField, StableFieldId("3b98ad0f-347e-44a9-8c95-e773c60767cb")]
    private UiButton? _button = null;

    protected override void OnEnable() => Bind();
    protected override void OnDisable() => Unbind();
    protected override void OnBeforeReload() => Unbind();
    protected override void OnAfterReload() => Bind();

    private void Bind()
    {
        if (_button is not null)
            _button.Clicked += Resume;
    }

    private void Unbind()
    {
        if (_button is not null)
            _button.Clicked -= Resume;
    }

    private void Resume() => Time.TimeScale = 1.0f;
}

[StableComponentId("fe661e70-c1a2-48b6-8719-b299a0f90acb")]
public sealed class PresentationExamples : Behaviour
{
    [SerializeField, StableFieldId("2fbf2b3f-8c43-458c-9eaf-c23099cadf0a")]
    private AudioClip? _footstep = null;

    [SerializeField, StableFieldId("c83d97cc-04e5-4989-b87e-178dc23b34bd")]
    private VfxEffect? _impact = null;

    public bool HasGround(Vector3 origin)
    {
        return Physics.TryRaycast(Entity, origin, new Vector3(0.0f, -1.0f, 0.0f), out RaycastHit hit,
            maximumDistance: 1.2f, ignoredEntity: Entity) && hit.Entity.IsValid;
    }

    public void PlayFootstep()
    {
        if (_footstep is not { IsValid: true })
            return;

        Audio.Play(Entity, _footstep, new AudioPlaybackOptions { Bus = "SFX", Gain = 0.8f, Spatial = true });
    }

    public void TintRenderer()
    {
        MeshRenderer? renderer = Entity.GetComponent<MeshRenderer>();
        if (renderer is null)
            return;

        renderer.PropertyBlock.SetColor("Tint", new Color(1.0f, 0.25f, 0.1f, 1.0f));
        renderer.PropertyBlock.SetFloat("Damage", 0.5f);
    }

    public void PlayImpact()
    {
        if (_impact is not { IsValid: true })
        {
            Debug.Warn("Impact VFX is not assigned.");
            return;
        }

        if (Vfx.Play(Entity, _impact, restart: true) is null)
            Debug.Warn("VFX playback request was rejected.");
    }
}

public static class JobExample
{
    public static async Task<int[]> BuildTableAsync(CancellationToken cancellation)
    {
        int[] table = new int[1024];
        Job job = Jobs.Submit(
            context =>
            {
                for (int index = 0; index < table.Length; ++index)
                {
                    context.CancellationToken.ThrowIfCancellationRequested();
                    table[index] = index * index;
                }
            },
            new JobDescription
            {
                Name = "Build lookup table",
                Priority = JobPriority.Low,
                Class = JobClass.Compute
            });

        using CancellationTokenRegistration registration = cancellation.Register(job.Cancel);
        await job.Completion;
        return table;
    }
}
