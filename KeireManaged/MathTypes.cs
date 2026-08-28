using System.Globalization;

namespace Keire;

// Record-generated formatting includes computed properties, so self-typed values such as Normalized would recurse.
// Keep every managed math value's text representation explicit and component-only.
public readonly record struct Vector2(float X, float Y)
{
    public static Vector2 Zero => default;
    public static Vector2 One => new(1.0f, 1.0f);
    public float LengthSquared => (X * X) + (Y * Y);
    public float Length => MathF.Sqrt(LengthSquared);
    public Vector2 Normalized => Length > 0.000001f ? this / Length : Zero;

    public static Vector2 operator +(Vector2 left, Vector2 right) => new(left.X + right.X, left.Y + right.Y);
    public static Vector2 operator -(Vector2 left, Vector2 right) => new(left.X - right.X, left.Y - right.Y);
    public static Vector2 operator -(Vector2 value) => new(-value.X, -value.Y);
    public static Vector2 operator *(Vector2 value, float scale) => new(value.X * scale, value.Y * scale);
    public static Vector2 operator *(float scale, Vector2 value) => value * scale;
    public static Vector2 operator /(Vector2 value, float scale) => new(value.X / scale, value.Y / scale);

    public override string ToString() =>
        string.Create(CultureInfo.InvariantCulture, $"Vector2 {{ X = {X}, Y = {Y} }}");
}

public readonly record struct Vector3(float X, float Y, float Z)
{
    public static Vector3 Zero => default;
    public static Vector3 One => new(1.0f, 1.0f, 1.0f);
    public static Vector3 Up => new(0.0f, 1.0f, 0.0f);
    public static Vector3 Forward => new(0.0f, 0.0f, 1.0f);
    public static Vector3 Right => new(1.0f, 0.0f, 0.0f);
    public float LengthSquared => (X * X) + (Y * Y) + (Z * Z);
    public float Length => MathF.Sqrt(LengthSquared);
    public Vector3 Normalized => Length > 0.000001f ? this / Length : Zero;

    public static float Dot(Vector3 left, Vector3 right) =>
        (left.X * right.X) + (left.Y * right.Y) + (left.Z * right.Z);

    public static Vector3 Cross(Vector3 left, Vector3 right) =>
        new((left.Y * right.Z) - (left.Z * right.Y),
            (left.Z * right.X) - (left.X * right.Z),
            (left.X * right.Y) - (left.Y * right.X));

    public static Vector3 Reflect(Vector3 direction, Vector3 normal) =>
        direction - (normal * (2.0f * Dot(direction, normal)));

    public static Vector3 Lerp(Vector3 from, Vector3 to, float amount) =>
        from + ((to - from) * Math.Clamp(amount, 0.0f, 1.0f));

    public static Vector3 operator +(Vector3 left, Vector3 right) =>
        new(left.X + right.X, left.Y + right.Y, left.Z + right.Z);
    public static Vector3 operator -(Vector3 left, Vector3 right) =>
        new(left.X - right.X, left.Y - right.Y, left.Z - right.Z);
    public static Vector3 operator -(Vector3 value) => new(-value.X, -value.Y, -value.Z);
    public static Vector3 operator *(Vector3 value, float scale) =>
        new(value.X * scale, value.Y * scale, value.Z * scale);
    public static Vector3 operator *(float scale, Vector3 value) => value * scale;
    public static Vector3 operator /(Vector3 value, float scale) =>
        new(value.X / scale, value.Y / scale, value.Z / scale);

    public override string ToString() =>
        string.Create(CultureInfo.InvariantCulture, $"Vector3 {{ X = {X}, Y = {Y}, Z = {Z} }}");
}

public readonly record struct Vector4(float X, float Y, float Z, float W)
{
    public override string ToString() =>
        string.Create(CultureInfo.InvariantCulture, $"Vector4 {{ X = {X}, Y = {Y}, Z = {Z}, W = {W} }}");
}

public readonly record struct Quaternion(float X, float Y, float Z, float W)
{
    public static Quaternion Identity => new(0.0f, 0.0f, 0.0f, 1.0f);

    public Quaternion Normalized
    {
        get
        {
            float length = MathF.Sqrt((X * X) + (Y * Y) + (Z * Z) + (W * W));
            return length > 0.000001f
                ? new Quaternion(X / length, Y / length, Z / length, W / length)
                : Identity;
        }
    }

    public static Quaternion Euler(float pitchDegrees, float yawDegrees, float rollDegrees = 0.0f)
    {
        float pitch = pitchDegrees * (MathF.PI / 180.0f) * 0.5f;
        float yaw = yawDegrees * (MathF.PI / 180.0f) * 0.5f;
        float roll = rollDegrees * (MathF.PI / 180.0f) * 0.5f;
        float sinPitch = MathF.Sin(pitch);
        float cosPitch = MathF.Cos(pitch);
        float sinYaw = MathF.Sin(yaw);
        float cosYaw = MathF.Cos(yaw);
        float sinRoll = MathF.Sin(roll);
        float cosRoll = MathF.Cos(roll);
        return new Quaternion(
            (sinPitch * cosYaw * cosRoll) - (cosPitch * sinYaw * sinRoll),
            (cosPitch * sinYaw * cosRoll) + (sinPitch * cosYaw * sinRoll),
            (cosPitch * cosYaw * sinRoll) - (sinPitch * sinYaw * cosRoll),
            (cosPitch * cosYaw * cosRoll) + (sinPitch * sinYaw * sinRoll)).Normalized;
    }

    public static Quaternion operator *(Quaternion left, Quaternion right) =>
        new((left.W * right.X) + (left.X * right.W) + (left.Y * right.Z) - (left.Z * right.Y),
            (left.W * right.Y) - (left.X * right.Z) + (left.Y * right.W) + (left.Z * right.X),
            (left.W * right.Z) + (left.X * right.Y) - (left.Y * right.X) + (left.Z * right.W),
            (left.W * right.W) - (left.X * right.X) - (left.Y * right.Y) - (left.Z * right.Z));

    public static Vector3 operator *(Quaternion rotation, Vector3 point)
    {
        Vector3 vector = new(rotation.X, rotation.Y, rotation.Z);
        Vector3 twiceCross = 2.0f * Vector3.Cross(vector, point);
        return point + (rotation.W * twiceCross) + Vector3.Cross(vector, twiceCross);
    }

    public override string ToString() =>
        string.Create(CultureInfo.InvariantCulture, $"Quaternion {{ X = {X}, Y = {Y}, Z = {Z}, W = {W} }}");
}

public readonly record struct Color(float Red, float Green, float Blue, float Alpha = 1.0f)
{
    public static Color White => new(1.0f, 1.0f, 1.0f, 1.0f);
    public static Color RedColor => new(1.0f, 0.0f, 0.0f, 1.0f);
    public static Color Lerp(Color from, Color to, float amount)
    {
        float t = Math.Clamp(amount, 0.0f, 1.0f);
        return new(from.Red + ((to.Red - from.Red) * t), from.Green + ((to.Green - from.Green) * t),
                   from.Blue + ((to.Blue - from.Blue) * t), from.Alpha + ((to.Alpha - from.Alpha) * t));
    }

    public override string ToString() => string.Create(
        CultureInfo.InvariantCulture,
        $"Color {{ Red = {Red}, Green = {Green}, Blue = {Blue}, Alpha = {Alpha} }}");
}
