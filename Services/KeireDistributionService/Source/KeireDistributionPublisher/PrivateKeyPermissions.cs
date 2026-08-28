using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;

namespace Keire.Distribution.Publisher;

internal static class PrivateKeyPermissions
{
    private const UnixFileMode AllowedUnixMode = UnixFileMode.UserRead | UnixFileMode.UserWrite;

    public static void Apply(FileStream stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        if (OperatingSystem.IsWindows())
        {
            ApplyWindows(stream);
            return;
        }

        if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS() || OperatingSystem.IsFreeBSD())
        {
            File.SetUnixFileMode(stream.Name, AllowedUnixMode);
            return;
        }

        throw UnsupportedPlatform();
    }

    public static void Validate(string path)
    {
        if (OperatingSystem.IsWindows())
        {
            ValidateWindows(path);
            return;
        }

        if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS() || OperatingSystem.IsFreeBSD())
        {
            UnixFileMode mode = File.GetUnixFileMode(path);
            if ((mode & UnixFileMode.UserRead) == 0 || (mode & ~AllowedUnixMode) != 0)
            {
                throw new InvalidDataException(
                    "The private-key file permissions are insecure; require owner read/write only (0600 or 0400).");
            }

            return;
        }

        throw UnsupportedPlatform();
    }

    [SupportedOSPlatform("windows")]
    private static void ApplyWindows(FileStream stream)
    {
        SecurityIdentifier currentUser = CurrentUserSid();
        FileSecurity security = new();
        security.SetAccessRuleProtection(isProtected: true, preserveInheritance: false);
        AddFullControl(security, currentUser);
        AddFullControl(security, new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null));
        AddFullControl(security, new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null));

        try
        {
            // The creator already owns a new file. Rewriting that same owner requires WRITE_OWNER, which ordinary
            // Windows users do not hold. Keep the exclusive handle open while replacing only the DACL so no other
            // process can open the empty file under its inherited permissions before it is restricted.
            new FileInfo(stream.Name).SetAccessControl(security);
            ValidateWindows(stream.Name);
        }
        catch (Exception exception) when (exception is UnauthorizedAccessException or NotSupportedException)
        {
            throw new InvalidDataException(
                "The private-key file ACL could not be restricted; use an explicitly named process environment variable instead.",
                exception);
        }
    }

    [SupportedOSPlatform("windows")]
    private static void ValidateWindows(string path)
    {
        SecurityIdentifier currentUser = CurrentUserSid();
        SecurityIdentifier localSystem = new(WellKnownSidType.LocalSystemSid, null);
        SecurityIdentifier administrators = new(WellKnownSidType.BuiltinAdministratorsSid, null);
        HashSet<SecurityIdentifier> allowed = [currentUser, localSystem, administrators];

        FileSecurity security;
        try
        {
            security = new FileInfo(path).GetAccessControl(AccessControlSections.Owner | AccessControlSections.Access);
        }
        catch (Exception exception) when (exception is UnauthorizedAccessException or NotSupportedException)
        {
            throw new InvalidDataException(
                "The private-key ACL could not be inspected; use an explicitly named process environment variable instead.",
                exception);
        }

        SecurityIdentifier? owner = security.GetOwner(typeof(SecurityIdentifier)) as SecurityIdentifier;
        if (owner is null || !allowed.Contains(owner) || !security.AreAccessRulesProtected)
        {
            throw new InvalidDataException("The private-key file ACL has an untrusted owner or inherited access rules.");
        }

        bool currentUserCanRead = false;
        AuthorizationRuleCollection rules = security.GetAccessRules(
            includeExplicit: true,
            includeInherited: true,
            targetType: typeof(SecurityIdentifier));
        foreach (FileSystemAccessRule rule in rules.Cast<FileSystemAccessRule>())
        {
            if (rule.AccessControlType != AccessControlType.Allow || rule.IdentityReference is not SecurityIdentifier sid)
            {
                continue;
            }

            if (!allowed.Contains(sid) && rule.FileSystemRights != 0)
            {
                throw new InvalidDataException("The private-key file ACL grants access to an untrusted identity.");
            }

            if (sid == currentUser && (rule.FileSystemRights & FileSystemRights.ReadData) != 0)
            {
                currentUserCanRead = true;
            }
        }

        if (!currentUserCanRead)
        {
            throw new InvalidDataException("The private-key file ACL does not grant the current user read access.");
        }
    }

    [SupportedOSPlatform("windows")]
    private static SecurityIdentifier CurrentUserSid()
    {
        using WindowsIdentity identity = WindowsIdentity.GetCurrent(TokenAccessLevels.Query);
        return identity.User ?? throw new InvalidDataException("The current Windows user has no security identifier.");
    }

    [SupportedOSPlatform("windows")]
    private static void AddFullControl(FileSecurity security, SecurityIdentifier identity)
    {
        security.AddAccessRule(new FileSystemAccessRule(
            identity,
            FileSystemRights.FullControl,
            InheritanceFlags.None,
            PropagationFlags.None,
            AccessControlType.Allow));
    }

    private static PlatformNotSupportedException UnsupportedPlatform()
    {
        return new PlatformNotSupportedException(
            "Private-key file permissions cannot be inspected on this platform; use an explicitly named process environment variable.");
    }
}
