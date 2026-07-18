# Managed SDK Consumer

This package-only example defines Keire's command-line description and application factory without defining `main`.
Linking only `Keire::Core` proves that the SDK's static Core archive supplies the managed entrypoint, resolves the
client contract, and carries the private Dear ImGui archive and SDL dependency transitively.

```sh
cmake -S . -B Build -DCMAKE_PREFIX_PATH=/path/to/extracted/sdk
cmake --build Build
./Build/ManagedSdkConsumer --managed-smoke
```
