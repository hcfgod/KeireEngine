using Keire;

namespace KeireSandbox;

[StableComponentId("73616e64-626f-4078-8000-000000000041")]
public sealed class HealthTarget : Behaviour, IDamageReceiver
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000042")]
    private float _maximumHealth = 100.0f;

    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000043")]
    private float _hitZoneMultiplier = 1.0f;

    [HotReloadState]
    private float _health;

    protected override void Awake()
    {
        _health = _health <= 0.0f ? _maximumHealth : Math.Clamp(_health, 0.0f, _maximumHealth);
        Damage.Register(Entity, this);
    }

    protected override void OnDestroy() => Damage.Unregister(Entity, this);

    public DamageResult ApplyDamage(in DamageInfo damage)
    {
        float applied = MathF.Max(0.0f, damage.Damage * _hitZoneMultiplier);
        _health = MathF.Max(0.0f, _health - applied);
        bool killed = _health <= 0.0f;
        Debug.Log($"{Entity.Name} took {applied:F1} damage ({_health:F1}/{_maximumHealth:F1}).");
        if (killed)
        {
            var entity = Entity;
            entity.Active = false;
        }
        return new DamageResult(applied, _health, killed, false);
    }
}
