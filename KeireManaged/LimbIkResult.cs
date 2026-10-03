namespace Keire;

public enum LimbIkSolveStatus : byte
{
    Disabled,
    Solved,
    Blended,
    Unreachable,
    NotConverged,
    InvalidInput,
    JointLimited
}

/// <summary>Published asset-bound limb result. All positions and distances are in skeleton model space.</summary>
public readonly record struct LimbIkResult(LimbId Id, LimbIkSolveStatus Status, Vector3 EndPosition,
                                          float PositionError, float ReachError, bool JointLimited);
