using System;
using System.Text;

namespace Keire;

internal static class AnimatorIkValidation
{
    internal static void Name(string value, string parameter)
    {
        ArgumentNullException.ThrowIfNull(value, parameter);
        if (string.IsNullOrWhiteSpace(value) || Encoding.UTF8.GetByteCount(value) > 256 || value.Contains('\0'))
            throw new ArgumentException("IK names must contain 1..256 UTF-8 bytes and no null characters.", parameter);
    }

    internal static void Vector(Vector3 value, string parameter)
    {
        if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z))
            throw new ArgumentOutOfRangeException(parameter, "IK positions must have finite coordinates.");
    }

    internal static void Goal(string goal, Vector3 target, float weight, AnimatorIkSpace space)
    {
        Name(goal, nameof(goal));
        Vector(target, nameof(target));
        if (!float.IsFinite(weight) || weight < 0 || weight > 1)
            throw new ArgumentOutOfRangeException(nameof(weight), "IK weight must be finite and between 0 and 1.");
        if (space != AnimatorIkSpace.Model && space != AnimatorIkSpace.World)
            throw new ArgumentOutOfRangeException(nameof(space), "IK space must be Model or World.");
    }
}
