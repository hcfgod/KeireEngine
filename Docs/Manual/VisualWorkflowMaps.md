# Visual Workflow Maps

Use these maps as a quick orientation when a task crosses Hub, Editor, graphs, scripts, assets, and player builds. Each
box is a user-visible product boundary; generated `Library/` state is deliberately absent because it is owned by the
Editor rather than authored by the project.

## From Project To Player

```mermaid
flowchart LR
    Hub["Kéire Hub<br/>choose project and Editor"] --> Editor["Editor<br/>author and validate"]
    Editor --> Assets["Assets/<br/>scenes, scripts, graphs, media"]
    Assets --> Play["Play Mode<br/>runtime copy of the scene"]
    Play --> Profile["Console and Profiler<br/>fix errors and budgets"]
    Profile --> Build["Build Settings<br/>cook a target player"]
    Build --> Player["Player folder<br/>executable and cooked data"]
```

The safe loop is author, wait for imports and script compilation, play, profile, stop, review Play Mode Changes, save,
and build. A Play Mode edit is not automatically a source-scene edit.

## What Owns What

```mermaid
flowchart TB
    Project["Project source"] --> Scene["Scene<br/>entities and components"]
    Project --> Script["C# assembly<br/>Behaviour and ScriptableObject types"]
    Project --> Graphs["Visual graphs<br/>Shader, Material, VFX"]
    Scene --> Entity["Entity"]
    Entity --> Native["Built-in components<br/>Transform, Collider, Audio Source, UI..."]
    Entity --> Behaviour["Managed Behaviours"]
    Script --> Behaviour
    Script --> Data["ScriptableObject assets"]
    Graphs --> Assign["Assignable assets<br/>materials and VFX effects"]
    Data -. "serialized reference" .-> Behaviour
    Assign -. "Inspector reference" .-> Native
    Assign -. "serialized reference" .-> Behaviour
```

Scripts should hold asset references through serialized fields rather than project-relative strings. Moving an asset
inside `Assets/` preserves its stable identity.

## Script Change And Reload

```mermaid
sequenceDiagram
    participant Author as You
    participant Compiler as Managed compiler
    participant Editor as Editor
    participant World as Play world
    Author->>Compiler: Save a C# source file
    Compiler->>Compiler: Build candidate assembly
    alt Candidate is valid
        Compiler->>Editor: Publish diagnostics and assembly
        Editor->>World: OnBeforeReload
        Editor->>World: Restore compatible serialized state
        Editor->>World: OnAfterReload
    else Candidate has errors
        Compiler->>Editor: Publish diagnostics
        Editor->>World: Keep last-good assembly running
    end
```

Unsubscribe from external events in both `OnDisable` and `OnBeforeReload`, then restore subscriptions in `OnEnable` or
`OnAfterReload`. See [C# Scripting Recipes](ScriptingRecipes.md) for a complete pattern.

## Visual Graph Publication

```mermaid
flowchart LR
    Edit["Edit draft<br/>nodes, blocks, properties"] --> Validate{"Candidate valid?"}
    Validate -->|No| Diagnostic["Show graph diagnostic<br/>retain editable draft"]
    Diagnostic --> Edit
    Validate -->|Yes| Preview["Update live preview"]
    Preview --> Save["Save source asset"]
    Save --> Cook["Cook target representation"]
    Cook --> Runtime["Scene or player uses asset"]
```

An older-looking preview commonly means the latest candidate failed and the last-good result is still displayed. Fix
the first graph diagnostic before changing unrelated nodes.

## Choose A Graph

```mermaid
flowchart TD
    Need{"What are you authoring?"}
    Need -->|"Reusable shader stages and reflected contract"| Shader["Shader Graph"]
    Need -->|"Assignable surface expressions"| Material["Material Graph"]
    Need -->|"Particles and ordered simulation"| Vfx["VFX Graph"]
    Shader --> Surface["Direct Material or Material Graph"]
    Material --> Renderer["Mesh Renderer material slot"]
    Surface --> Renderer
    Vfx --> Emitter["VFX Emitter component or Vfx.Play"]
```

Continue with [Shader Graph Examples](ShaderGraphExamples.md), [Material Graph Examples](MaterialGraphExamples.md),
and [VFX Graph Examples](VfxGraphExamples.md).
