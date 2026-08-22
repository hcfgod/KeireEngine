# VFX Graph

Kéire VFX combines a typed executable graph, ordered Blocks, effect settings, Blackboard properties, a bounded preview,
and scene-owned VFX Emitter components. The VFX Effect panel's execution/backend badges are authoritative for the open
asset.

## Create An Effect

1. In Project, choose **Create > VFX Effect**.
2. Name the `.keirevfx` asset and double-click it.
3. In the VFX Effect panel, confirm the execution-source and backend badges.
4. Right-click the graph to add Contexts, Operators, Blackboard values, and ordered Blocks.
5. Configure **Effect Settings**: loop, duration, simulation space, seed, and capacity.
6. Compile, fix every graph diagnostic, and save.
7. Add a **VFX Emitter** component to an entity and assign the effect.

Required Spawn, Initialize, Update, and Output flow depends on the effect. Context anchors are protected. Blocks execute
in their authored order inside a Context; Operators provide typed values through cables. Starting search from a pin
filters the catalog to compatible choices.

## Preview

The panel preview owns a transient draft, not the persisted scene emitter. When an eligible scene emitter uses the open
asset, the editor may route the draft through that host transform and seed offset. Hiding the panel stops its transient
handle. Entering Play Mode stops edit-scene preview handles and transfers presentation ownership to the runtime scene.

An incomplete required connection freezes the last valid preview and blocks Save. Repair the topology or undo the
edit; Kéire keeps the editable draft, positions, selection, and undo history.

## VFX Subgraphs

`.keirevfxsubgraph` schema 1 supports three explicit purposes:

- **Operator**: reusable typed value computation;
- **Block**: reusable ordered Block behavior;
- **System**: reusable complete system graph.

Subgraphs validate purpose, typed boundaries, dependencies, recursion, and bounded expansion before lowering. They do
not imply the remainder of another engine's node catalog, and there is no general selection-to-subgraph conversion in
0.4.0.

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
        Debug.Warn("VFX playback request was rejected.");
}
```

The runtime scene must be active, the entity must be valid, and capacity/budget validation may reject a request. A VFX
Emitter wrapper controls play, pause, resume, stop, event delivery, properties, and status. Simulation-space behavior
is explicit; entity scale does not scale VFX shapes in the current transform contract.

## Performance Checks

Profile particle capacity, alive counts, compute thread groups, CPU work, and fence-completion latency together. A
preview is not a performance gate. Test representative emitter counts in the Game view and in a cooked player.

Use [Graph Editing](GraphEditing.md) for shared canvas operations and [VFX Authoring and Runtime](../Vfx.md) for the
complete operator/block catalog, compatibility execution, CPU/GPU boundaries, diagnostics, and budgets.
