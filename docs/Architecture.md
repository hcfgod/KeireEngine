# Architecture

## Ownership

`Core` is a static C++20 library and owns reusable application behavior, including logging. `Client` is the runnable application and depends on Core. `Tests` is a separate executable that depends on Core and doctest. Each project owns a local `premake5.lua`; the root file only defines workspace identity and loads projects.

`Config/Project.conf` defines names and folders. `Config/Dependencies.lock` defines immutable external inputs. Premake and launchers read these files so renaming and dependency verification have one source of truth.

## Automation Flow

```mermaid
flowchart LR
    Launcher["Platform launcher"] --> Identity["Identity and dependency locks"]
    Launcher --> Bootstrap["Tool and vendor verification"]
    Launcher --> Generate["Premake generation"]
    Generate --> Build["Compiler or IDE build"]
    Build --> Tests["doctest and Client smoke run"]
    Tests --> Coverage["LLVM coverage"]
    Tests --> Package["Runtime and SDK archive"]
```

The scripts resolve `default` to a concrete compiler before generation. Architecture defaults to the native host and may be overridden with x86_64 or ARM64. A generation stamp prevents reuse with mismatched generator, architecture, toolset, or CI warning policy.

## Logging Lifecycle

Core owns a private spdlog thread pool and two asynchronous loggers. The implementation does not register global names, replace the spdlog default, or shut down unrelated state. A shared lifecycle lock protects logging handles; shutdown takes exclusive ownership and waits for active handles before destroying sinks and workers.

File paths are intentionally relative to the process working directory. Scripts and generated IDE targets set that directory to the repository root, producing consistent `Logs/Core.log` and `Logs/Client.log` paths.

## Release Shape

Packages include the Client runtime, Core static library, public Core headers, required spdlog headers, complete spdlog/fmt/doctest license texts, notices, README, and a validated machine-readable build manifest. Release debug symbols are uploaded separately where a platform toolchain emits them; Dist is intentionally stripped.
