# VFX Graph

Kéire VFX combines a typed executable graph, ordered Blocks, effect settings, Blackboard properties, a bounded preview,
and scene-owned VFX Emitter components. The VFX Effect panel's execution/backend badges are authoritative for the open
asset.

## Create An Effect

1. Open a project outside Play Mode. In Project, choose **Create > VFX Effect**.
2. Name the `.keirevfx` asset and double-click it.
3. In the VFX Effect panel, confirm the execution-source and backend badges.
4. Inspect the initial graph before adding anything. Right-click the graph to add Contexts, Operators, Blackboard
   values, and ordered Blocks; keep the required flow connected.
5. Configure **Effect Settings**: loop, duration, simulation space, seed, and capacity.
6. Compile, fix every graph diagnostic, and save.
7. Drag the effect into the Scene to create a positioned **VFX Emitter**, or add the component to an existing entity
   and assign the effect in its Inspector.
8. Save the scene. Reopen the effect and scene to check the graph and emitter reference, then test in Game view.

Expected result: a valid emitting graph produces particles in preview and through its scene emitter. An effect asset
alone is not a persistent scene placement. Keep capacity modest for the first test and check the dropped count.

Required Spawn, Initialize, Update, and Output flow depends on the effect. Context anchors are protected. Blocks execute
in their authored order inside a Context; Operators provide typed values through cables. Starting search from a pin
filters the catalog to compatible choices.

## Preview

The panel preview owns a transient draft, not the persisted scene emitter. When an eligible scene emitter uses the open
asset, the editor may route the draft through that host transform and seed offset. Hiding the panel stops its transient
handle. Entering Play Mode stops edit-scene preview handles and transfers presentation ownership to the runtime scene.

Use the restart and pause/resume icons for the transient preview. **Loop Preview** repeats that preview; it is not the
same as **Effect Settings > Loop**, which is saved with the effect. **Backend** selects the preview backend and
**Speed** changes preview speed. The GPU particle count is labeled **spawned (estimate)**; do not interpret it as an
exact live-particle count. **PREVIEW FROZEN** means the current draft is not publishable.

An incomplete required connection freezes the last valid preview and blocks Save. Repair the topology or undo the
edit; Kéire keeps the editable draft, positions, selection, and undo history.

## VFX Subgraphs

`.keirevfxsubgraph` schema 1 supports three explicit purposes:

- **Operator**: reusable typed value computation;
- **Block**: reusable ordered Block behavior;
- **System**: reusable complete system graph.

Subgraphs validate purpose, typed boundaries, dependencies, recursion, and bounded expansion before lowering. They do
not imply the remainder of another engine's node catalog. Do not infer a VFX selection-extraction command from the
separate Shader Graph function-extraction feature.

## Use An Authored Particle Material

1. Create a **Shader Graph > VFX** asset and expose a Color parameter. Connect it to the VFX output's Base Color.
2. Save the graph, select it, and choose **Material from Shader**. Set the material's color to an obvious green.
3. Open the VFX Effect and assign this material to its Sprite or Ribbon output's **Material** field.
4. Preview the effect, then assign it to a scene VFX Emitter and check it in Game view.
5. Change the material color to red. CPU and GPU Sprite/Ribbon rendering should use the updated authored shader.
6. Finish the Material Inspector edit, explicitly save the effect and scene, then reopen them and check the references.

Particle Vertex Color supplies particle tint, while UV0/UV1 provide output coordinates for texture sampling. Add these
nodes to the shader when the graph should use those particle values. Authored textures are material properties; the
output's legacy Sprite texture is the built-in fallback input. Volumetric output still uses built-in density shading.
The Shader Graph thumbnail is not a particle simulation. See [VFX rendering details](../Vfx.md) for backend constraints.

## Runtime Control

Assign a `VfxEffect` and drive it with the managed service:

```csharp
[SerializeField, StableFieldId("c83d97cc-04e5-4989-b87e-178dc23b34bd")]
private VfxEffect? _impact = null;

private void PlayImpact()
{
    if (_impact is not { IsValid: true })
    {
        Debug.Warn("Impact VFX is not assigned.");
        return;
    }

    if (Vfx.Play(Entity, _impact, restart: true) is null)
    {
        Debug.Warn("VFX playback request was rejected.");
    }
}
```

This is a field and method excerpt for a managed component, not a standalone script. Assign the serialized effect
reference in the Inspector and call the method from your gameplay event.

The runtime scene must be active, the entity must be valid, and capacity/budget validation may reject a request. A VFX
Emitter wrapper controls play, pause, resume, stop, event delivery, properties, and status. Simulation-space behavior
is explicit; entity scale does not scale VFX shapes in the current transform contract.

## Performance Checks

Profile particle capacity, alive counts, compute thread groups, CPU work, and fence-completion latency together. A
preview is not a performance gate. Test representative emitter counts in the Game view and in a cooked player.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Save or restart is disabled | Repair the first graph diagnostic; the last-good preview does not validate the draft. |
| One-shot effect repeats only in preview | Disable **Loop Preview** before diagnosing the saved effect's Loop setting. |
| Effect exists but is absent from the scene | Assign it to a VFX Emitter and check its position, runtime state, and camera. |
| Color edit does not affect particles | Check the output Material assignment and whether its shader consumes particle color. |
| Particles are dropped | Check capacity and runtime budgets before raising the emission rate. |
| Backend results differ | Inspect backend diagnostics and supported operations; compare in a target player. |

Use [Graph Editing](GraphEditing.md) for shared canvas operations and [VFX Authoring and Runtime](../Vfx.md) for the
complete operator/block catalog, compatibility execution, CPU/GPU boundaries, diagnostics, and budgets.
