using Keire;
using System.Runtime.InteropServices;

internal static unsafe class CharacterGravityTests
{
    private static byte _status;
    private static Vector3 _acceleration;
    private static int _calls;

    internal static void Run()
    {
        var previous = NativeRuntime.GetCharacterGravityIcall;
        var previousExists = NativeRuntime.EntityExistsIcall;
        var previousComponentExists = NativeRuntime.ComponentExistsIcall;
        NativeRuntime.GetCharacterGravityIcall = &Sample;
        NativeRuntime.EntityExistsIcall = &Exists;
        NativeRuntime.ComponentExistsIcall = &ComponentExists;
        try
        {
            var character = new CharacterController(new Entity(12, new EntityId(34, 56)));
            _status = 1;
            _acceleration = new Vector3(3, -4, 5);
            _calls = 0;
            Check(character.TryGetGravityAcceleration(out var value) && value == _acceleration && _calls == 1);
            _acceleration = Vector3.Zero;
            Check(character.TryGetGravityAcceleration(out value) && value == Vector3.Zero);
            _status = 0;
            _acceleration = new Vector3(99, 99, 99);
            Check(!character.TryGetGravityAcceleration(out value) && value == Vector3.Zero);
            _status = 1;
            _acceleration = new Vector3(float.NaN, 0, 0);
            Check(!character.TryGetGravityAcceleration(out value) && value == Vector3.Zero);
            NativeRuntime.GetCharacterGravityIcall = null;
            Check(!character.TryGetGravityAcceleration(out value) && value == Vector3.Zero);
        }
        finally
        {
            NativeRuntime.GetCharacterGravityIcall = previous;
            NativeRuntime.EntityExistsIcall = previousExists;
            NativeRuntime.ComponentExistsIcall = previousComponentExists;
        }
    }

    private static void Check(bool condition)
    {
        if (!condition)
        {
            throw new InvalidOperationException("Character gravity context or availability contract failed.");
        }
    }

    [UnmanagedCallersOnly]
    private static byte Exists(ulong world, ulong high, ulong low) =>
        (byte)(world == 12 && high == 34 && low == 56 ? 1 : 0);

    [UnmanagedCallersOnly]
    private static byte ComponentExists(ulong world, ulong high, ulong low, ulong typeHigh, ulong typeLow) =>
        (byte)(world == 12 && high == 34 && low == 56 ? 1 : 0);

    [UnmanagedCallersOnly]
    private static byte Sample(ulong world, ulong high, ulong low, Vector3* acceleration)
    {
        ++_calls;
        if (world != 12 || high != 34 || low != 56 || acceleration == null)
        {
            return 0;
        }
        *acceleration = _acceleration;
        return _status;
    }
}
