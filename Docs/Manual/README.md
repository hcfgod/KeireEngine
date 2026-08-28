# Kéire 0.4.3 User Manual

This manual is for people creating and shipping projects with Kéire 0.4.3. It starts in Kéire Hub, follows the
day-to-day Editor workflow, introduces the supported C# gameplay API, and ends with player and content packages. It is
deliberately separate from the engine-maintainer material in [Architecture](../Architecture.md).

Kéire 0.4.3 is a pre-1.0 source and Windows publication candidate. Use the Hub's actual installed versions and
component status when deciding what can be launched or built; signed 0.4.2 Windows and Linux packages remain active
through catalog sequence 17 until the new Windows packages are activated.

## Follow The Learning Path

| Step | Guide | Outcome |
| ---: | --- | --- |
| 1 | [Projects and Editor](ProjectsAndEditor.md) | Create or open a project and understand the default workspace. |
| 2 | [C# Scripting Fundamentals](ScriptingFundamentals.md) | Build and attach a Behaviour with correct lifecycle cleanup. |
| 3 | [Entities, Prefabs, Assets, and Scenes](WorldAndAssets.md) | Work with component handles and persistent content. |
| 4 | [Input, Physics, and Audio](InputPhysicsAndAudio.md) | Connect authored data to frame and fixed-step gameplay. |
| 5 | [UI, Jobs, and Diagnostics](UiJobsAndDiagnostics.md) | Build responsive UI and background work with visible failures. |
| 6 | [Shader Graph](ShaderGraph.md) | Author a reusable renderer-facing shader contract. |
| 7 | [Material Graph](MaterialGraph.md) | Build assignable surface logic against a Shader Graph. |
| 8 | [VFX Graph](VfxGraph.md) | Author, preview, attach, and control a visual effect. |
| 9 | [Graph Editing](GraphEditing.md) | Use selection, comments, collapse, clipboard, and reuse consistently. |
| 10 | [Debugging and Profiling](DebuggingAndProfiling.md) | Diagnose scripts, assets, graphs, and frame performance. |
| 11 | [Player Builds and Packages](PlayerBuildsAndPackages.md) | Cook a desktop player and move content safely. |
| 12 | [C# API Quick Reference](CSharpApiQuickReference.md) | Find the supported Unity-shaped gameplay surface quickly. |
| 13 | [Visual Workflow Maps](VisualWorkflowMaps.md) | See how projects, scripts, graphs, scenes, and players fit together. |
| 14 | [C# Scripting Recipes](ScriptingRecipes.md) | Adapt complete gameplay patterns for movement, reload, scenes, and presentation. |
| 15 | [Shader Graph Examples](ShaderGraphExamples.md) | Build tinted, emissive, and height-blended shader contracts. |
| 16 | [Material Graph Examples](MaterialGraphExamples.md) | Build damage, wetness, instance, and global-parameter surfaces. |
| 17 | [VFX Graph Examples](VfxGraphExamples.md) | Build looping, burst, event-driven, and reusable particle systems. |

## Product Vocabulary

- **Hub** owns project discovery, installed Editor versions, compatibility checks, and Build Support components.
- **Editor** owns authoring, Play Mode, imports, graphs, C# compilation, profiling, and player-build requests.
- **Runtime** runs a validated cooked player without Editor UI or source-content ownership.
- **Project panel** is the asset browser. Source content belongs below `Assets/`; generated state belongs below
  `Library/` and should not be edited or committed.
- **Entity** is Kéire's runtime GameObject-shaped object. Components and Behaviours attach to entities.
- **Asset** is a stable-ID project resource. Moving an asset in `Assets/` does not require scripts to keep a path.

## A Safe Working Loop

1. Open the project through Hub and confirm the selected Editor is healthy and compatible.
2. Author source assets in the Project panel and scenes in the Hierarchy/Inspector.
3. Wait for imports and managed compilation to finish; read Console diagnostics before entering Play Mode.
4. Test in Play Mode. Runtime scene edits use a separate history and are not automatically written to the source
   scene.
5. Stop, review **Play Mode Changes**, apply only intended changes, and save with `Ctrl/Cmd+S`.
6. Profile representative content before creating a player.
7. Build through **Build > Build Settings**, then run the produced executable on its target platform.

## Where To Go Deeper

These user guides lead with workflows and supported outcomes. The complete documentation library remains useful when a
tool reports a schema, migration, or packaging failure:

- [C# scripting guide set](../Scripting/README.md)
- [Scene authoring reference](../SceneAuthoring.md)
- [Shaders and materials reference](../ShadersAndMaterials.md)
- [VFX authoring and runtime reference](../Vfx.md)
- [Structured diagnostic remediation](../Diagnostics/README.md)

The principal C# snippets are also maintained in a
[compile-checkable example project](Examples/Keire.ManualExamples.csproj). Build it from the repository root with
`dotnet build Docs/Manual/Examples/Keire.ManualExamples.csproj`.
