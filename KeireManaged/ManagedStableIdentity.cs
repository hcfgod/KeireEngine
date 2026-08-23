using System.Reflection;
using System.Security.Cryptography;
using System.Text;

namespace Keire;

internal static class ManagedStableIdentity
{
    internal static Guid Field(MemberInfo member, Guid parent)
    {
        ArgumentNullException.ThrowIfNull(member);
        StableFieldIdAttribute? declared = member.GetCustomAttribute<StableFieldIdAttribute>(true);
        return declared?.Id ?? Derive(parent, $"field:{member.Name}");
    }

    internal static Guid Derive(Guid parent, string suffix)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(suffix);
        byte[] value = SHA256.HashData(Encoding.UTF8.GetBytes($"{parent:D}/{suffix}"));
        value[6] = (byte)((value[6] & 0x0F) | 0x50);
        value[8] = (byte)((value[8] & 0x3F) | 0x80);
        return new Guid(value.AsSpan(0, 16));
    }
}
