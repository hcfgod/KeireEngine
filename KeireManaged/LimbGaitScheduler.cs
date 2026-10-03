namespace Keire;

/// <summary>An immutable ordered set of limbs that lift together after landing admission succeeds.</summary>
public sealed class LimbGaitGroup
{
    internal LimbId[] Ids { get; }
    public IReadOnlyList<LimbId> Limbs { get; }

    public LimbGaitGroup(params LimbId[] limbs)
    {
        ArgumentNullException.ThrowIfNull(limbs);
        if (limbs.Length == 0 || limbs.Length > LimbGaitScheduler.MaximumLimbs)
            throw new ArgumentException("A gait group needs 1 to 64 limbs.", nameof(limbs));
        Ids = (LimbId[])limbs.Clone();
        LimbGaitScheduler.ValidateIds(Ids, nameof(limbs));
        Limbs = Array.AsReadOnly(Ids);
    }
}

/// <summary>A caller-defined support region whose minimum planted count must survive each voluntary lift.</summary>
public sealed class LimbSupportRequirement
{
    internal LimbId[] Ids { get; }
    public IReadOnlyList<LimbId> Limbs { get; }
    public int MinimumPlanted { get; }

    public LimbSupportRequirement(int minimumPlanted, params LimbId[] limbs)
    {
        ArgumentNullException.ThrowIfNull(limbs);
        if (limbs.Length == 0 || limbs.Length > LimbGaitScheduler.MaximumLimbs || minimumPlanted < 0 || minimumPlanted > limbs.Length)
            throw new ArgumentOutOfRangeException(nameof(minimumPlanted));
        Ids = (LimbId[])limbs.Clone();
        LimbGaitScheduler.ValidateIds(Ids, nameof(limbs));
        MinimumPlanted = minimumPlanted;
        Limbs = Array.AsReadOnly(Ids);
    }
}

/// <summary>
/// Deterministic, simulation-thread-owned grouped stepping. Physics, swing timing, contact transport,
/// reach, and geometric balance remain caller-owned. Missing contacts never count as support.
/// This count/region policy is not a support-polygon or dynamic balance proof.
/// </summary>
public sealed class LimbGaitScheduler
{
    public const int MaximumLimbs = 64;
    public const int MaximumGroups = 64;
    private readonly LimbId[] _limbs;
    private readonly int[][] _preferred;
    private readonly int[][] _fallback;
    private readonly (int Minimum, int[] Indices)[] _requirements;
    private readonly bool[] _planted;
    private readonly bool[] _swinging;
    private readonly int _minimumPlanted;
    private readonly bool _preferredRequiresAllPlanted;
    private readonly int _maximumGroupSize;
    private int _preferredCursor;
    private int _fallbackCursor;

    public IReadOnlyList<LimbId> Limbs { get; }
    public int SupportingCount { get; private set; }
    public int SwingingCount { get; private set; }
    public int GroupCount => _preferred.Length + _fallback.Length;

    public LimbGaitScheduler(ReadOnlySpan<LimbId> limbs, IReadOnlyList<LimbGaitGroup> preferredGroups,
        IReadOnlyList<LimbGaitGroup> fallbackGroups, int minimumPlanted,
        IReadOnlyList<LimbSupportRequirement>? supportRequirements = null, bool preferredRequiresAllPlanted = true)
    {
        ArgumentNullException.ThrowIfNull(preferredGroups);
        ArgumentNullException.ThrowIfNull(fallbackGroups);
        if (limbs.Length == 0 || limbs.Length > MaximumLimbs)
            throw new ArgumentException("Register 1 to 64 limbs.", nameof(limbs));
        ValidateIds(limbs, nameof(limbs));
        if (minimumPlanted < 1 || minimumPlanted > limbs.Length)
            throw new ArgumentOutOfRangeException(nameof(minimumPlanted), "At least one planted support must remain.");
        if (preferredGroups.Count + (long)fallbackGroups.Count is < 1 or > MaximumGroups)
            throw new ArgumentException("Configure 1 to 64 gait groups.");
        if (supportRequirements?.Count > MaximumLimbs)
            throw new ArgumentException("At most 64 support requirements are allowed.", nameof(supportRequirements));
        _limbs = limbs.ToArray();
        Limbs = Array.AsReadOnly(_limbs);
        _planted = new bool[limbs.Length];
        _swinging = new bool[limbs.Length];
        _minimumPlanted = minimumPlanted;
        _preferredRequiresAllPlanted = preferredRequiresAllPlanted;
        _preferred = MapGroups(preferredGroups);
        _fallback = MapGroups(fallbackGroups);
        foreach (var group in _preferred) _maximumGroupSize = Math.Max(_maximumGroupSize, group.Length);
        foreach (var group in _fallback) _maximumGroupSize = Math.Max(_maximumGroupSize, group.Length);
        _requirements = new (int, int[])[supportRequirements?.Count ?? 0];
        for (int index = 0; index < _requirements.Length; ++index)
        {
            var requirement = supportRequirements![index] ?? throw new ArgumentException("Null support requirement.");
            _requirements[index] = (requirement.MinimumPlanted, Map(requirement.Ids));
        }
    }

    /// <summary>Seeds fresh observed contacts in Limbs order and resets both group cursors.</summary>
    public void Initialize(ReadOnlySpan<bool> planted)
    {
        RequireCount(planted.Length, nameof(planted));
        planted.CopyTo(_planted);
        Array.Clear(_swinging);
        SupportingCount = 0;
        foreach (bool value in planted) if (value) ++SupportingCount;
        SwingingCount = _preferredCursor = _fallbackCursor = 0;
    }

    public bool IsPlanted(LimbId limb) => _planted[Index(limb)];
    public bool IsSwinging(LimbId limb) => _swinging[Index(limb)];

    /// <summary>Commit only after a confirmed contact, never from swing timer completion alone.</summary>
    public void CompletePlant(LimbId limb)
    {
        int index = Index(limb);
        if (!_planted[index]) ++SupportingCount;
        if (_swinging[index]) --SwingingCount;
        _planted[index] = true;
        _swinging[index] = false;
    }

    public void LoseContact(LimbId limb)
    {
        int index = Index(limb);
        if (_planted[index]) --SupportingCount;
        if (_swinging[index]) --SwingingCount;
        _planted[index] = _swinging[index] = false;
    }

    /// <summary>
    /// Lists missing contacts first for caller-controlled recovery probes, in registration order.
    /// Does not manufacture a plant or lift surviving supports. Destination must fit all registered limbs.
    /// </summary>
    public int CopyRecoveryCandidates(Span<LimbId> destination)
    {
        if (destination.Length < _limbs.Length) throw new ArgumentException("Insufficient recovery output capacity.", nameof(destination));
        int count = 0;
        for (int index = 0; index < _limbs.Length; ++index)
            if (!_planted[index] && !_swinging[index]) destination[count++] = _limbs[index];
        return count;
    }

    /// <summary>
    /// Atomically lifts one admissible group, returning its stable IDs in configured order. NeedsStep and
    /// safeLanding use Limbs order. At least one group member must request movement and all must be planted
    /// with safe destinations. No second group starts while any limb swings. Preferred and fallback groups
    /// have independent round-robin cursors. Optional groupAdmission uses preferred-then-fallback order;
    /// callers may use it for geometric balance/collision vetoes without invoking callbacks during mutation.
    /// All arguments are validated before changing any state or output. No result leaves output untouched.
    /// </summary>
    public int TryBeginStep(ReadOnlySpan<bool> needsStep, ReadOnlySpan<bool> safeLanding, Span<LimbId> selected,
        ReadOnlySpan<bool> groupAdmission = default)
    {
        RequireCount(needsStep.Length, nameof(needsStep));
        RequireCount(safeLanding.Length, nameof(safeLanding));
        if (selected.Length < _maximumGroupSize) throw new ArgumentException("Insufficient lift output capacity.", nameof(selected));
        if (!groupAdmission.IsEmpty && groupAdmission.Length != GroupCount)
            throw new ArgumentException("Group admission must cover every configured group.", nameof(groupAdmission));
        if (SwingingCount != 0) return 0;
        if (!_preferredRequiresAllPlanted || SupportingCount == _limbs.Length)
        {
            int count = TryGroups(_preferred, ref _preferredCursor, 0, needsStep, safeLanding, selected, groupAdmission);
            if (count != 0) return count;
        }
        return TryGroups(_fallback, ref _fallbackCursor, _preferred.Length, needsStep, safeLanding, selected, groupAdmission);
    }

    private int TryGroups(int[][] groups, ref int cursor, int admissionOffset, ReadOnlySpan<bool> needs,
        ReadOnlySpan<bool> safe, Span<LimbId> selected, ReadOnlySpan<bool> admission)
    {
        for (int offset = 0; offset < groups.Length; ++offset)
        {
            int index = (cursor + offset) % groups.Length;
            var group = groups[index];
            if ((!admission.IsEmpty && !admission[admissionOffset + index]) || SupportingCount - group.Length < _minimumPlanted)
                continue;
            bool eligible = true, requested = false;
            ulong removing = 0;
            foreach (int limb in group)
            {
                eligible &= _planted[limb] && safe[limb];
                requested |= needs[limb];
                removing |= 1UL << limb;
            }
            if (!eligible || !requested) continue;
            foreach (var requirement in _requirements)
            {
                int remaining = 0;
                foreach (int limb in requirement.Indices)
                    if (_planted[limb] && (removing & (1UL << limb)) == 0) ++remaining;
                eligible &= remaining >= requirement.Minimum;
            }
            if (!eligible) continue;
            for (int output = 0; output < group.Length; ++output)
            {
                int limb = group[output];
                _planted[limb] = false;
                _swinging[limb] = true;
                selected[output] = _limbs[limb];
            }
            SupportingCount -= group.Length;
            SwingingCount += group.Length;
            cursor = (index + 1) % groups.Length;
            return group.Length;
        }
        return 0;
    }

    private int[][] MapGroups(IReadOnlyList<LimbGaitGroup> groups)
    {
        var result = new int[groups.Count][];
        for (int index = 0; index < groups.Count; ++index)
            result[index] = Map((groups[index] ?? throw new ArgumentException("Null gait group.")).Ids);
        return result;
    }
    private int[] Map(LimbId[] limbs)
    {
        var result = new int[limbs.Length];
        for (int index = 0; index < limbs.Length; ++index) result[index] = Index(limbs[index]);
        return result;
    }
    private int Index(LimbId limb)
    {
        int index = Array.IndexOf(_limbs, limb);
        return index >= 0 ? index : throw new ArgumentException("Unknown limb identifier.", nameof(limb));
    }
    private void RequireCount(int count, string name)
    {
        if (count != _limbs.Length) throw new ArgumentException("Input must match the registered limb count.", name);
    }
    internal static void ValidateIds(ReadOnlySpan<LimbId> limbs, string name)
    {
        for (int index = 0; index < limbs.Length; ++index)
        {
            if (!limbs[index].IsValid) throw new ArgumentException("Limb identifiers must be nonzero.", name);
            for (int prior = 0; prior < index; ++prior)
                if (limbs[index] == limbs[prior]) throw new ArgumentException("Limb identifiers must be unique.", name);
        }
    }
}
