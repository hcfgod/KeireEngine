# Managed Extensibility

[Scripting home](README.md) · [Workflow map](WorkflowMap.md) · [Cookbook](Cookbook.md) · [API index](ApiIndex.md)

Kéire separates gameplay and authoring code at the assembly boundary. Runtime `.keireasm` assemblies reference
`Keire.Managed.dll`; Editor assemblies reference both `Keire.Managed.dll` and `Keire.Editor.Managed.dll`. Editor and
generator assemblies are staged with the Editor and approved headless tools, but are excluded from cooked players.
Tests use only their declared assembly references.

Every accepted reload is a new immutable generation. Discovery uses the exact assembly/type allowlist produced by the
validated `.keireasm` graph. Runtime services, native contracts, and Editor extensions are staged before publication;
any conflict, constructor failure, migration failure, or managed exception cancels the candidate and leaves the last
good generation active. Generation tokens reject stale calls, cancellation tokens end with the generation, and
retired extensions stop in reverse order before their assembly load context unloads.

## On This Page

- [Custom Serialized Values](#custom-serialized-values)
- [Runtime Services](#runtime-services)
- [Native Source-Module Contracts](#native-source-module-contracts)
- [Editor SDK](#editor-sdk)
- [Choose An Editor Extension](#choose-an-editor-extension)
- [Editor Examples And Setup](#editor-examples-and-setup)
- [Stability Rules](#stability-rules)

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

## Choose An Editor Extension

These are supported managed contracts with different levels of native Editor integration. Read the
[capability matrix](ManagedApiMatrix.md) before treating a contract or a compiled example as evidence that every
native window/control/build invocation path is available in your installed Editor. Remaining work includes complete
retained-tree presentation, hard-process importer execution, layout restoration, safe-mode UI, and package-stage
build invocation. The examples below exercise the contract; they do not close those integration gaps.

| Task | Extension/API | How to implement it |
| --- | --- | --- |
| Draw one serialized type | `PropertyDrawer`, `CustomPropertyDrawer` | Return a tree from `CreatePropertyGUI`; stage edits on the supplied property |
| Decorate attributed fields | `PropertyAttribute`, `PropertyDecorator` | Return a wrapped tree from `Decorate`; keep attribute types in the appropriate shared boundary |
| Replace an object's Inspector | `Editor`, `CustomEditor`, `CanEditMultipleObjects` | Return `CreateInspectorGUI`; handle mixed values through `SerializedObject` |
| Add a retained window | `EditorWindow`, `EditorWindowAttribute` | Build `RootVisualElement` in `CreateGUI`; remove event listeners on disable |
| Add settings | `SettingsProvider` | Supply a stable settings path and return `CreateSettingsGUI` |
| Add a scene interaction tool | `EditorTool`, `EditorToolAttribute`, `SceneToolContext` | Implement activation, scene callback, and deactivation; honor context lifetime |
| Add menus or debug drawings | `MenuItem`, `DrawGizmo`, handle APIs | Keep the declaring type registered; use the attributed signature and supported target types |
| Observe authoring state | `Selection`, `EditorApplication` | Subscribe once per extension instance and unsubscribe before retirement |
| Import a custom source format | `ScriptedImporter`, `ScriptedImporterAttribute` | Read via context, emit validated artifact DTOs, select a main output |
| React to imported/deleted/moved content | `AssetPostprocessor`, `AssetChangeBatch` | Keep reactions bounded and use Asset Database transactions for further authoring |
| Change project assets | `AssetDatabase`, `Undo` | Use the scoped authoring transaction; do not edit live files behind the database |
| Validate or stamp a build | `IPreprocessBuild`, `IPostprocessBuild`, `BuildContext` | Order processors deterministically; write only via `WriteStagedFile` |

Every discovered extension needs its own `EditorExtensionId`. Every target registration must remain unambiguous.
The [Editor reference](EditorReference.md) includes exact constructors, callbacks, and registration attributes.
Engine lifecycle activation supplies `Lifetime`; constructing an extension with `new` in a plain console does not
activate it or connect it to native Editor services.

## Editor Examples And Setup

The [Editor example project](EditorExamples/Keire.EditorExamples.csproj) compiles against both the Editor SDK and the
[runtime examples](Examples/Keire.ScriptingExamples.csproj). For a project integration:

1. Put shared persisted types such as `MovementSettings` in a Runtime-classified assembly.
2. Put extension scripts under a predefined `Editor` folder or an explicitly Editor-classified assembly.
3. If both are custom assemblies, add the runtime assembly's ID to the editor assembly's references. Custom
   assemblies cannot reference predefined assemblies; move shared types into a custom runtime assembly first.
4. Keep Editor extension IDs stable and unique, wait for successful compilation/catalog discovery, and inspect
   extension diagnostics when an extension is unavailable or quarantined.
5. Test the contract through its supported Editor/host invocation path. An importer or build processor is not a
   Behaviour to attach to a scene, and its discovery alone does not run an import/build.

### Selection Window

[SelectionSummary.cs](EditorExamples/SelectionSummary.cs) builds a label and reacts to `Selection.Changed`.
`CreateGUI` clears the old tree before rebuilding it; `OnDisable` removes the generation's listener. Use this
pattern for windows that observe Editor state rather than doing polling work every frame.

```csharp
protected override void OnEnable() => Selection.Changed += Refresh;
protected override void OnDisable() => Selection.Changed -= Refresh;

public override void CreateGUI()
{
    RootVisualElement.Clear();
    RootVisualElement.Add(_summary);
    Refresh();
}
```

### Multi-Object Inspector

[MovementSettingsEditor.cs](EditorExamples/MovementSettingsEditor.cs) provides standard property fields plus a
button that stages `Speed = 4` and commits all selected targets through one named undo transaction.
`CanEditMultipleObjects` advertises this intentional multi-target behavior. To preserve individual mixed values,
leave a property unchanged until the user explicitly edits it; do not initialize it from the first target merely
because a tree is being created.

```csharp
Lifetime.ThrowIfInvalid();
SerializedObject.Update();
if (SerializedObject.FindProperty(nameof(MovementSettings.Speed)) is { } speed)
{
    speed.BoxedValue = 4.0f;
    SerializedObject.ApplyModifiedProperties("Set movement speed");
}
```

`BoxedValue` stages a value and checks the declared type; `ApplyModifiedProperties` is the commit boundary.
Do not mutate `Target` fields directly. Reacquire snapshots/properties after updates or generation changes rather
than caching them indefinitely in a callback closure.

### Custom Importer

[NotesImporter.cs](EditorExamples/NotesImporter.cs) handles the example-only `.gamenotes` extension, reads text through
`AssetImportContext`, emits a `TextImportArtifact` under the stable sub-asset key `notes`, and chooses it as the main
output. A sample source can contain plain UTF-8 notes. The host's import pipeline supplies the bounded reader and
consumes the artifacts; gameplay scripts do not execute the importer.

```csharp
context.CancellationToken.ThrowIfCancellationRequested();
string text = context.ReadSourceText(context.AssetPath);
context.AddObject("notes", new TextImportArtifact(text) { Name = "Game notes" });
context.SetMainObject("notes");
```

Use context dependency methods for extra source/asset inputs. Stable output keys preserve deterministic sub-asset
identity; bump the importer version when changing its output contract. Do not claim a built-in extension or write
directly into `Library` or the live database. Artifact constructors do not bypass the import pipeline's validation.

### Build Processor

[BuildStamp.cs](EditorExamples/BuildStamp.cs) implements `IPostprocessBuild` with order `100` and writes deterministic
product/version metadata through `BuildContext`. Its complete output is staged until the pipeline accepts every
processor and validation result. It intentionally contains no wall-clock timestamp, so identical build descriptions
produce identical bytes.

```csharp
string stamp = $"{context.Description.ProductName}\n{context.Description.Version}\n";
context.WriteStagedFile("Metadata/game-version.txt", Encoding.UTF8.GetBytes(stamp));
```

Paths must remain relative to the staging directory; root paths, traversal, and empty path segments are rejected.
Honor cancellation and report errors through the context. Use a host that invokes `BuildExtensionPipeline`; do not
assume current packaged-player build commands invoke this hook at every package stage.

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

