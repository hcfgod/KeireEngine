# Kéire 0.4.8 User Manual

Start here to create a project, author a scene, add gameplay and UI, and build a standalone player. Each guide explains
what to prepare, which controls or APIs to use, what a successful result looks like, and where to investigate a failure.
You do not need to understand the engine's internal architecture to follow the authoring path.

Kéire 0.4.8 is pre-1.0. The Windows Hub hot patch uses signed catalog revision 27 and keeps the 0.4.8 display version;
the published Editor is also 0.4.8. The catalog retains older independently validated Linux packages. The presence of
a target in Build Settings is not proof that its Build Support is installed or that a player has been tested on that OS.
Use Hub's actual installed-version and component status to decide what you can launch or build.

For installation or sign-in problems, begin with [Hub Installation and Recovery](HubInstallationAndRecovery.md).
For project backups, moves, source control, or template cleanup, use
[Back Up, Share, and Recover a Project](ProjectMaintenance.md).

## Choose the Right Tool

| You want to… | Use… | Start with… |
| --- | --- | --- |
| Create a project or install an Editor. | Hub. | [Projects and Editor](ProjectsAndEditor.md). |
| Build a scene and assign a material. | Hierarchy, Project, and Inspector. | [Materials](MaterialGraph.md). |
| Change a surface's color, texture, or exposed value. | The material Inspector. | [Material Recipes](MaterialGraphExamples.md). |
| Create custom shader logic or a camera effect. | Shader Graph, then a material referencing its shader. | [Shader Graph](ShaderGraph.md). |
| Create a particle effect. | VFX Graph. | [VFX Graph](VfxGraph.md). |
| Add behavior, input, or interactions. | A C# Behaviour in a project assembly. | [Scripting Fundamentals](ScriptingFundamentals.md). |
| Create a game HUD or menu. | UI Builder, UI documents, and C# bindings. | [UI, Jobs, and Diagnostics](UiJobsAndDiagnostics.md). |
| Produce an executable for players. | Build Settings and matching Build Support. | [Player Builds and Packages](PlayerBuildsAndPackages.md). |

New materials use shader selection and properties in the Inspector. A **Material Graph** name in an old guide, a
legacy source, or an internal type does not mean that a new material needs a graph canvas. Existing graph-owned
assets are a compatibility workflow; use the material guide before converting them.

## Follow The Learning Path

For a first project, follow **Projects and Editor → Materials → Scripting Fundamentals → Build Your Own Game UI →
Test Gameplay Before You Ship → Player Builds and Packages**. Use the topic guides below when you need more detail.

| Step | Guide | Outcome |
| ---: | --- | --- |
| 1 | [Projects and Editor](ProjectsAndEditor.md) | Create or open a project and understand the default workspace. |
| 2 | [C# Scripting Fundamentals](ScriptingFundamentals.md) | Build and attach a Behaviour with correct lifecycle cleanup. |
| 3 | [Entities, Prefabs, Assets, and Scenes](WorldAndAssets.md) | Work with component handles and persistent content. |
| 4 | [Input, Physics, and Audio](InputPhysicsAndAudio.md) | Connect authored data to frame and fixed-step gameplay. |
| 5 | [UI, Jobs, and Diagnostics](UiJobsAndDiagnostics.md) | Build responsive UI and background work with visible failures. |
| 6 | [Materials](MaterialGraph.md) | Create a material, choose its shader, edit Inspector properties, and assign it to a renderer. |
| 7 | [Shader Graph](ShaderGraph.md) | Author custom shader logic when material properties alone are not enough. |
| 8 | [VFX Graph](VfxGraph.md) | Author, preview, attach, and control a visual effect. |
| 9 | [Graph Editing](GraphEditing.md) | Use selection, comments, collapse, clipboard, and reuse consistently. |
| 10 | [Debugging and Profiling](DebuggingAndProfiling.md) | Diagnose scripts, assets, graphs, and frame performance. |
| 11 | [Player Builds and Packages](PlayerBuildsAndPackages.md) | Cook a desktop player and move content safely. |
| 12 | [C# API Quick Reference](CSharpApiQuickReference.md) | Find the supported Unity-shaped gameplay surface quickly. |
| 13 | [Visual Workflow Maps](VisualWorkflowMaps.md) | See how projects, scripts, graphs, scenes, and players fit together. |
| 14 | [C# Scripting Recipes](ScriptingRecipes.md) | Adapt complete gameplay patterns for movement, reload, scenes, and presentation. |
| 15 | [Shader Graph Examples](ShaderGraphExamples.md) | Build tinted, emissive, height-blended, and camera fullscreen effects. |
| 16 | [Material Recipes](MaterialGraphExamples.md) | Reuse shaders, vary material properties, and choose an appropriate override scope. |
| 17 | [VFX Graph Examples](VfxGraphExamples.md) | Build looping, burst, event-driven, and reusable particle systems. |

## Product Vocabulary

- **Hub** owns project discovery, installed Editor versions, compatibility checks, and Build Support components.
- **Editor** owns authoring, Play Mode, imports, graphs, C# compilation, profiling, and player-build requests.
- **Material** selects a shader and supplies its exposed values; **Shader Graph** authors the shader's executable logic.
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

These user guides lead with workflows and supported outcomes. References explain contracts, formats, and edge cases;
engineering plans and dated acceptance records preserve history. Follow the user guide for a current task rather than
interpreting a historical open item or internal class name as a menu instruction.

- [C# scripting guide set](../Scripting/README.md)
- [Scene authoring reference](../SceneAuthoring.md)
- [Shaders and materials reference](../ShadersAndMaterials.md)
- [VFX authoring and runtime reference](../Vfx.md)
- [Structured diagnostic remediation](../Diagnostics/README.md)

The principal C# snippets are also maintained in a
[compile-checkable example project](Examples/Keire.ManualExamples.csproj). Build it from the repository root with
`dotnet build Docs/Manual/Examples/Keire.ManualExamples.csproj`. Use the SDK selected by
`Config/Dependencies.lock`; an older machine-wide `dotnet` may not support the examples' target framework. Do not
retarget the examples to hide an SDK mismatch.
