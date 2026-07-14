# SDK Consumer

This example consumes an extracted SDK package; it is intentionally not part of the Premake workspace.

```sh
cmake -S . -B Build -DCMAKE_PREFIX_PATH=/path/to/extracted/sdk
cmake --build Build
./Build/SdkConsumer Client.json
```

The package provides the imported `Keire::Core` target with its C++20, include-directory, SDL static target, and platform link requirements. The consumer exercises public ownership, event, time, configuration, and window APIs to validate the complete dependency chain.
