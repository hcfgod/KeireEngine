# Managed Extensibility

Kéire separates gameplay and authoring code at the assembly boundary. Runtime `.keireasm` assemblies reference
`Keire.Managed.dll`; Editor assemblies reference both `Keire.Managed.dll` and `Keire.Editor.Managed.dll`. Editor and
generator assemblies are staged with the Editor and approved headless tools, but are excluded from cooked players.
Tests use only their declared assembly references.

Every accepted reload is a new immutable generation. Discovery uses the exact assembly/type allowlist produced by the
validated `.keireasm` graph. Runtime services, native contracts, and Editor extensions are staged before publication;
any conflict, constructor failure, migration failure, or managed exception cancels the candidate and leaves the last
good generation active. Generation tokens reject stale calls, cancellation tokens end with the generation, and
retired extensions stop in reverse order before their assembly load context unloads.

## Custom Serialized Values

Ordinary `[Serializable]` classes and structs remain composed automatically. Use a converter only when a type should
be one atomic value or is otherwise unsupported:

```csharp
[CustomManagedValueConverter(typeof(Angle), "8933c0de-e741-4961-a808-c4775751e87f", 2)]
public sealed class AngleConverter : ManagedValueConverter<Angle>
{
    public override ManagedSerializedValue Write(Angle value) =>
        ManagedSerializedValue.From(value.Degrees);

    public override Angle Read(ManagedSerializedValue value) =>
        new(value.AsNumber());
}
```

Converters match the exact target type. Stable converter IDs and versions become persisted schema, so never reuse an
ID for a different meaning. Payloads are bounded null, Boolean, integer, finite-number, UTF-8 string, list, or
string-keyed map values. They cannot contain an engine object or conceal an engine reference. Duplicate target types
or stable IDs reject the candidate generation.

Format v4 is the canonical writer; readers for v1 through v3 remain available. A custom record stores `$custom`,
`version`, and a canonical `payload`. Use `[ManagedValueMigration]` implementations from Runtime assemblies to upgrade
old payload versions. Migration edges must form one unique contiguous chain to the active converter version. Kéire
migrates a temporary document in stable type/object/field order and writes the upgraded form only on an explicit save
or cook.

Classes may implement `ISerializationCallbackReceiver`. `OnBeforeSerialize` and `OnAfterDeserialize` run root-first on
fully staged graphs on the managed owner thread. `Behaviour.OnValidate` runs for Editor-authored candidates. Any
callback failure rejects the staged graph without modifying the live object.

## Runtime Services

An application-owned service is a concrete parameterless `IRuntimeService` in a Runtime assembly:

```csharp
[RuntimeService("15fc3cfb-696c-438d-9214-bca8dd0d8138")]
public sealed class WeatherService : IRuntimeService, IRuntimeServiceHotReloadState
{
    public void Start(RuntimeServiceContext context) { }
    public void Update(RuntimeServiceUpdateContext context) { }
    public void Stop() { }

    public ManagedSerializedValue CaptureState() => ManagedSerializedValue.Null;
    public void RestoreState(ManagedSerializedValue state) { }
}
```

Declare edges with `[RuntimeServiceDependency(typeof(OtherService))]`. Kéire topologically orders services and uses
stable IDs as the deterministic tie-breaker. A cycle or required-service startup failure rejects the candidate;
optional-service failures quarantine that service. Updates run on the application owner thread, services receive a
generation lifetime token, hot-reload state crosses generations as a bounded canonical document, and shutdown runs in
reverse dependency order. Services are created per application/runtime world and are never mutable process globals.

## Native Source-Module Contracts

Source modules register `ManagedServiceDescriptor` and `ManagedBindingMethodDescriptor` values through
`ModuleRegistrationContext`. Managed declarations use `[NativeServiceContract]` and `[NativeMethod]`. The
`Keire.Managed.Generators` incremental generator emits typed calls and rejects unsupported signatures at compile time.
The candidate publishes only when managed and native stable service/method IDs, ABI versions, thread affinity,
parameters, bounded spans, structured-result shape, and completeness match exactly.

The public ABI permits Booleans, bounded integers and finite floats, UTF-8 strings, Kéire math values, stable
IDs/handles, bounded spans, and structured errors. Raw pointers, unmanaged ownership, arbitrary object graphs, and
unbounded buffers are forbidden.

## Editor SDK

`Keire.Editor.Managed.dll` exposes retained-UI authoring contracts in `Keire.Editor`:

- generation-scoped `SerializedObject` and `SerializedProperty` snapshots with stable paths, nested collection
  traversal, mixed values, staged writes, and one atomic `ApplyModifiedProperties(undoName)` transaction;
- `PropertyAttribute`, property drawers/decorators, custom editors, and multi-object editing through
  `CreatePropertyGUI` or `CreateInspectorGUI` returning `VisualElement` trees;
- scripted importers, bounded `AssetImportContext` source reads, deterministic sub-assets, validated artifact DTOs,
  postprocessors, and importer editors;
- dockable windows, menus, settings providers, scene tools, gizmos/handles, selection, Undo, preferences,
  project-scoped singletons, and transactional Asset Database operations;
- ordered pre-build and post-stage processors with immutable build descriptions and staging-only writes.

All registrations require `[EditorExtensionId]`. Drawer/editor resolution prefers the exact type and then the nearest
registered base type that opts into children; an equally specific registration rejects the catalog. Scripted importer
extensions must be unique and cannot silently replace built-ins. Retained window and extension instances receive
generation lifetimes, Editor callbacks are isolated into structured diagnostics, and a failing drawer/window/tool is
quarantined while the built-in presentation remains available.

Importer requests and build writes are bounded and deterministic. The `ScriptedImportRequest` cache key includes the
importer ID/version, assembly fingerprint, normalized settings, source digest, target, and sorted dependencies.
`BuildExtensionPipeline` holds output in a staging map until every ordered processor and validation callback succeeds.
The native worker remains the process-isolation boundary: managed import/build code must never receive an unrestricted
filesystem path or publish directly into the live asset database or package tree.

## Stability Rules

- Persisted converters, migrations, Behaviours, ScriptableObjects, reference-graph types, runtime services, and native
  contracts belong to Runtime assemblies.
- Editor assemblies may change presentation and authoring workflows, but cannot introduce a player-required persisted
  type.
- Keep extension, converter, migration, service, contract, method, component, asset-type, and field IDs stable once
  data or integrations ship.
- Treat every callback argument, `SerializedObject`, `SerializedProperty`, context, writer, token, and retained element
  as generation-scoped. Do not cache it beyond the documented lifetime.
- Never bypass `SerializedObject`, `AssetImportContext`, `AssetDatabase`, or `BuildContext` with direct live-data or
  filesystem mutation.

