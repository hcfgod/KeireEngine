# Managed SDK Consumer

This package-only example validates both sides of the managed SDK contract. The C++ consumer defines Kéire's
command-line description and application factory without defining `main`. Linking only `Keire::Core` proves that the
SDK's static Core archive supplies the managed entrypoint, resolves the client contract, and carries the private Dear
ImGui archive and SDL dependency transitively.

`ManagedApiConsumer.csproj` compiles `ManagedPresentationAssets.cs` against the packaged `Keire.Managed.dll`. The
example demonstrates typed Audio, Material, Shader Graph, Material Graph, and VFX residency leases with explicit
reverse-order disposal.

```sh
cmake -S . -B Build -DCMAKE_PREFIX_PATH=/path/to/extracted/sdk
cmake --build Build
./Build/ManagedSdkConsumer --managed-smoke
dotnet build ManagedApiConsumer.csproj --configuration Release
```
