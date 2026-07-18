# Documentation

Kéire documentation is organized by task. The root [README](../README.md) is the project overview and command
reference; these guides provide the details needed to develop, integrate, validate, and release the engine.

| Guide | Purpose | Primary audience |
| --- | --- | --- |
| [Getting Started](GettingStarted.md) | Clone, bootstrap, generate, build, run, clean, and diagnose a workstation | New contributors and integrators |
| [Architecture](Architecture.md) | System boundaries, ownership, implementation strategy, and release shape | Engine maintainers |
| [Runtime Lifecycle](RuntimeLifecycle.md) | Startup, frame order, threading, layer mutation, events, time, UI, and shutdown | Runtime and layer authors |
| [UI Workspace](UiWorkspace.md) | Panel registration, factory docking, layouts, themes, persistence, and recovery | Editor and tooling authors |
| [Testing And Release](TestingAndRelease.md) | Test matrix, sanitizers, smoke tests, scripts, packages, and handoff checks | Maintainers and release engineers |

## Sources Of Truth

Documentation explains the repository but does not replace its authoritative inputs:

- `AGENTS.md` defines repository-wide engineering and validation rules.
- `Config/Project.conf` defines project identity and target names.
- `Config/Dependencies.lock` defines immutable dependency repositories and commits.
- `.clang-format` defines first-party C++ formatting.
- Premake files define Kéire targets and compiler policy.
- Platform launchers under `Scripts/` define supported developer workflows.

When documentation and an authoritative input disagree, correct the documentation in the same change that resolves the
discrepancy.

## Documentation Maintenance

Keep guides task-oriented and link to the supported public API rather than private implementation types. Public API,
ownership, packaging, or workflow changes should update the relevant guide, the root README when user-facing behavior
changes, and `CHANGELOG.md` when engine or SDK users are affected.

Code snippets are production examples. They must use the public `Keire/` headers, follow `.clang-format`, and remain
consistent with the supported lifecycle and thread-affinity contracts.
