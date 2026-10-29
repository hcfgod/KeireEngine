using System.Globalization;
using System.Runtime.InteropServices;

namespace Keire;

/// <summary>Double-precision coordinates within one reference frame; carries no frame identity.</summary>
[StructLayout(LayoutKind.Sequential)]
public readonly record struct Vector3d(double X, double Y, double Z)
{
    public bool IsFinite => double.IsFinite(X) && double.IsFinite(Y) && double.IsFinite(Z);

    public static Vector3d Widen(Vector3 value) => new(value.X, value.Y, value.Z);

    /// <summary>
    /// Subtracts a same-frame origin before narrowing. The error budget is per axis, in metres.
    /// </summary>
    public Vector3 RelativeFloat(Vector3d origin, double maximumError)
    {
        if (!IsFinite || !origin.IsFinite || !double.IsFinite(maximumError) || maximumError < 0.0)
        {
            throw new ArgumentException("Relative coordinates require finite positions and a nonnegative error budget.");
        }

        return new Vector3(
            Narrow(X - origin.X, maximumError),
            Narrow(Y - origin.Y, maximumError),
            Narrow(Z - origin.Z, maximumError));
    }

    private static float Narrow(double value, double maximumError)
    {
        if (!double.IsFinite(value) || Math.Abs(value) > float.MaxValue)
        {
            throw new ArgumentOutOfRangeException(nameof(value), "Relative coordinate exceeds the float range.");
        }

        float narrowed = (float)value;
        if (Math.Abs((double)narrowed - value) > maximumError)
        {
            throw new ArgumentOutOfRangeException(nameof(value), "Relative coordinate exceeds its precision budget.");
        }

        return narrowed;
    }

    public override string ToString() =>
        string.Create(CultureInfo.InvariantCulture, $"Vector3d {{ X = {X}, Y = {Y}, Z = {Z} }}");
}
