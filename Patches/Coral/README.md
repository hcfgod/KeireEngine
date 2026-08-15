# Kéire Coral patch policy

Kéire pins Coral at `d53b2685725f7535bc4d1deaa8a22bf16d112fe2`. Runtime integration must apply the
numbered patches in this directory in order and fail if a patch no longer applies cleanly. Use
`Scripts/Windows/coral.ps1` or `Scripts/Unix/coral.sh`; both create a commit-and-patch-digest-keyed copy outside
`Vendor`. Passing the build option also validates that a .NET 10 SDK is installed and builds the native and managed
host outputs.

The patch boundary is deliberately outside `Vendor`: Coral remains an immutable upstream input, while Kéire owns the
following portability and lifetime changes until they can be accepted upstream:

1. discover HostFXR through `nethost` instead of a hard-coded runtime-major path;
2. store host state per Kéire `ScriptSystem`, with no process-global runtime owner;
3. make context and host shutdown idempotent and safe after partial initialization;
4. pass filesystem paths through UTF-8 conversions on every supported platform;
5. allow nethost to resolve hostfxr from an application-owned bundled .NET root;
6. release an existing managed handle before move-assignment so collectible reload cannot leak objects into a
   retiring load context;
7. apply host settings before HostFXR discovery so an application-owned bundled `.NET` root is actually honored;
8. keep Coral's native host warning-clean under MSVC without hiding warnings outside its deprecated compatibility
   implementation;
9. key reflected managed methods by runtime `Type` identity so two collectible reload contexts cannot share stale
   `MethodInfo` objects for equal full names;
10. assign load contexts and assemblies collision-safe opaque IDs, resolve dependency search paths with the native
    platform separator, and retain an explicit failure status for unclassified load exceptions;
11. give memory-loaded assemblies the same owner-context and local-type caches as file-loaded assemblies;
12. assign process-wide reflection metadata collision-safe opaque IDs that cannot alias after an identity-hash
    collision or cache retirement;
13. make assembly, context, and reflected-method caches safe for concurrent Kéire runtime hosts, while keeping load
    failure status local to the calling thread and synchronizing debug GC-handle diagnostics;
14. keep Windows, Linux, and macOS x64/ARM64 code paths buildable.

Do not modify a downloaded Coral tree in place. The dependency bootstrap must copy it into a commit-keyed build cache,
apply this patch set there, and record both the upstream commit and patch-set digest in the dependency manifest.
