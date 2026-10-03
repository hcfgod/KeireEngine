using System.Collections.ObjectModel;
using System.Globalization;

namespace Keire;

/// <summary>A rig-local persistent limb identity. Zero is unassigned.</summary>
public readonly record struct LimbId(uint Value)
{
    public bool IsValid => Value != 0;
}

public enum LimbIkSolver : byte
{
    TwoBone,
    Fabrik
}

/// <summary>Immutable named chain settings. Bone existence and ancestry are checked by the native evaluator.</summary>
public sealed class LimbIkDefinition
{
    public LimbId Id { get; }
    public string Name { get; }
    public IReadOnlyList<string> Bones { get; }
    public LimbIkSolver Solver { get; }
    public uint MaximumIterations { get; }
    public float Tolerance { get; }

    public LimbIkDefinition(LimbId id, string name, IEnumerable<string> bones,
                            LimbIkSolver solver = LimbIkSolver.Fabrik,
                            uint maximumIterations = 32, float tolerance = 0.001f)
    {
        if (!id.IsValid)
            throw new ArgumentException("A limb requires a nonzero persistent ID.", nameof(id));
        AnimatorIkValidation.Name(name, nameof(name));
        ArgumentNullException.ThrowIfNull(bones);
        if (!Enum.IsDefined(solver))
            throw new ArgumentOutOfRangeException(nameof(solver));
        var copy = bones.Take(257).ToArray();
        if (copy.Length < 2 || copy.Length > 256 || (solver == LimbIkSolver.TwoBone && copy.Length != 3))
            throw new ArgumentException("Two-bone limbs require three bones; FABRIK limbs require 2..256.", nameof(bones));
        var unique = new HashSet<string>(StringComparer.Ordinal);
        foreach (string bone in copy)
        {
            AnimatorIkValidation.Name(bone, nameof(bones));
            if (bone.Contains('\u001f') || !unique.Add(bone))
                throw new ArgumentException("Limb bones must be unique and cannot contain U+001F.", nameof(bones));
        }
        if (maximumIterations is 0 or > 1024)
            throw new ArgumentOutOfRangeException(nameof(maximumIterations));
        if (!float.IsFinite(tolerance) || tolerance <= 0)
            throw new ArgumentOutOfRangeException(nameof(tolerance));
        Id = id;
        Name = name;
        Bones = Array.AsReadOnly(copy);
        Solver = solver;
        MaximumIterations = maximumIterations;
        Tolerance = tolerance;
    }
}

/// <summary>A goal in an explicit coordinate space. A default value has zero weight.</summary>
public readonly record struct LimbIkTarget(Vector3 Position, Vector3 Pole, float Weight = 1,
                                           AnimatorIkSpace Space = AnimatorIkSpace.World);

/// <summary>
/// Reuses immutable chain definitions through the supported Animator goal API. Each instance must own a unique
/// goalNamespace on its Animator. Submit in OnAnimatorIk; call ClearAll in OnDisable while the Animator is alive.
/// Submission queues a goal; it does not claim the native pose has reached it or that terrain clearance is satisfied.
/// </summary>
public sealed class LimbIkRig
{
    private readonly Animator _animator;
    private readonly Dictionary<LimbId, (LimbIkDefinition Definition, string Goal)> _bindings = new();
    public IReadOnlyList<LimbIkDefinition> Limbs { get; }

    public LimbIkRig(Animator animator, string goalNamespace, IEnumerable<LimbIkDefinition> limbs)
    {
        ArgumentNullException.ThrowIfNull(animator);
        ArgumentNullException.ThrowIfNull(limbs);
        AnimatorIkValidation.Name(goalNamespace, nameof(goalNamespace));
        var definitions = limbs.Take(257).ToArray();
        if (definitions.Length is 0 or > 256)
            throw new ArgumentException("A rig requires 1..256 limbs.", nameof(limbs));
        foreach (var definition in definitions)
        {
            ArgumentNullException.ThrowIfNull(definition, nameof(limbs));
            string goal = goalNamespace + "/" + definition.Id.Value.ToString(CultureInfo.InvariantCulture);
            AnimatorIkValidation.Name(goal, nameof(goalNamespace));
            if (!_bindings.TryAdd(definition.Id, (definition, goal)))
                throw new ArgumentException("Each limb must have a unique persistent ID.", nameof(limbs));
        }
        _animator = animator;
        Limbs = new ReadOnlyCollection<LimbIkDefinition>(definitions);
    }

    public void SetTarget(LimbId limb, LimbIkTarget target)
    {
        var binding = Binding(limb);
        var definition = binding.Definition;
        AnimatorIkValidation.Goal(binding.Goal, target.Position, target.Weight, target.Space);
        AnimatorIkValidation.Vector(target.Pole, nameof(target));
        if (definition.Solver == LimbIkSolver.TwoBone)
            _animator.SetTwoBoneIK(binding.Goal, definition.Bones[0], definition.Bones[1], definition.Bones[2],
                                   target.Position, target.Pole, target.Weight, target.Space);
        else
            _animator.SetFabrikIK(binding.Goal, definition.Bones, target.Position, target.Weight,
                                  definition.MaximumIterations, definition.Tolerance, target.Space);
    }

    public bool Clear(LimbId limb) => _animator.ClearIK(Binding(limb).Goal);

    /// <summary>Removes only this rig's goals. Native failure is propagated; already cleared goals remain cleared.</summary>
    public void ClearAll()
    {
        foreach (var definition in Limbs)
            Clear(definition.Id);
    }

    private (LimbIkDefinition Definition, string Goal) Binding(LimbId limb) =>
        _bindings.TryGetValue(limb, out var binding)
            ? binding
            : throw new ArgumentException("The limb is not registered in this rig.", nameof(limb));
}
