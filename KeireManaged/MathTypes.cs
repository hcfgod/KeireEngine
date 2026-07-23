namespace Keire;

public readonly record struct Vector2(float X, float Y)
{
    public static Vector2 operator +(Vector2 left, Vector2 right) => new(left.X + right.X, left.Y + right.Y);
    public static Vector2 operator -(Vector2 left, Vector2 right) => new(left.X - right.X, left.Y - right.Y);
    public static Vector2 operator *(Vector2 value, float scale) => new(value.X * scale, value.Y * scale);
}

public readonly record struct Vector3(float X, float Y, float Z)
{
    public static Vector3 operator +(Vector3 left, Vector3 right) =>
        new(left.X + right.X, left.Y + right.Y, left.Z + right.Z);
    public static Vector3 operator -(Vector3 left, Vector3 right) =>
        new(left.X - right.X, left.Y - right.Y, left.Z - right.Z);
    public static Vector3 operator *(Vector3 value, float scale) =>
        new(value.X * scale, value.Y * scale, value.Z * scale);
}

public readonly record struct Vector4(float X, float Y, float Z, float W);
public readonly record struct Quaternion(float X, float Y, float Z, float W)
{
    public static Quaternion Identity => new(0.0f, 0.0f, 0.0f, 1.0f);
}
public readonly record struct Color(float Red, float Green, float Blue, float Alpha = 1.0f);
