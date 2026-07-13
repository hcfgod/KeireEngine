# SDK Consumer

This example consumes an extracted SDK package; it is intentionally not part of the Premake workspace.

```sh
cmake -S . -B Build -DCMAKE_PREFIX_PATH=/path/to/extracted/sdk
cmake --build Build
```

The package provides the imported `Core::Core` target with its C++20, include-directory, and platform link requirements.
