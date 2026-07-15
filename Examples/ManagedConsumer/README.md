# Managed SDK Consumer

This package-only example defines Keire's command-line description and application factory without defining `main`.
Linking it proves that the SDK's static Core archive supplies the managed entrypoint and resolves the client contract.

```sh
cmake -S . -B Build -DCMAKE_PREFIX_PATH=/path/to/extracted/sdk
cmake --build Build
./Build/ManagedSdkConsumer --managed-smoke
```
