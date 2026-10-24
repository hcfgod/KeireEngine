# VFX Graph Examples

VFX Graph combines ordered particle-stream Contexts with typed value cables. Blocks run top-to-bottom inside a Context;
Operators calculate values. A connected Spawn or Event source, Initialize, Update, and Output path must remain acyclic
and forward-only. Spawn systems need enabled emission, and every executable effect needs an enabled Renderer.

## Example 1: Looping Campfire Embers

```mermaid
flowchart LR
    Spawn["Spawn Context<br/>Emission Rate"] --> Init["Initialize Context<br/>Box Shape<br/>lifetime and upward velocity"]
    Init --> Update["Update Context<br/>Forces<br/>Size over Lifetime<br/>Color over Lifetime"]
    Update --> Output["Output Context<br/>Renderer"]
    Rate["Spawn Rate parameter"] -. "float" .-> Spawn
    Lift["Lift parameter"] -. "Vector3" .-> Update
```

1. Start from the default graph so the canonical Context chain, Emission Rate, and Renderer are present.
2. Enable Loop and choose a moderate capacity and emission rate.
3. Add a Box Shape in Initialize, then use a multi-second lifetime and a small randomized upward velocity range.
4. Add Forces, Size over Lifetime, and Color over Lifetime Blocks in Update.
5. Compile after each stage. Tune capacity and emission together while watching alive counts in the Profiler.

Start with a small test budget, for example capacity 128 and emission 16 particles per second. Use a two-second
lifetime before adding curves. Expected result: a continuous, bounded stream; the simple rate-times-lifetime estimate
is about 32 live particles before other behavior changes. Save the effect, assign a scene emitter, and check the
result there. GPU preview counts are spawned estimates, not exact alive counts.

Place the effect on a VFX Emitter entity at the fire. Simulation space determines whether existing particles follow a
moving emitter; entity scale does not scale VFX shapes in the current transform contract.

## Example 2: One-Shot Impact Burst

```mermaid
flowchart LR
    Spawn["Spawn Context<br/>Burst only"] --> Init["Initialize Context<br/>Sphere or cone shape<br/>short lifetime<br/>outward velocity"]
    Init --> Update["Update Context<br/>collision or force<br/>fade and shrink"]
    Update --> Output["Output Context<br/>Renderer"]
```

1. Add and enable a Burst, then remove or disable Emission Rate.
2. Initialize a short lifetime with an outward velocity range.
3. Fade color/alpha and reduce size over lifetime in Update.
4. Keep capacity close to the maximum simultaneous impacts multiplied by particles per burst.
5. Attach a VFX Emitter or invoke `Vfx.Play(Entity, effect, restart: true)` from a validated serialized `VfxEffect`.

For the first test, use a burst of 16 particles and a one-second lifetime, disable the saved **Loop** setting, and
turn off **Loop Preview**. Restart preview once: one burst should appear and then die out, rather than emit forever.
Save the effect and scene, reopen them, and repeat the test. Increase the burst only after this baseline works.

If `Vfx.Play` returns `null`, the runtime scene, entity, capacity, or budget rejected the request. Log that as a
recoverable presentation failure rather than assuming an effect started.

## Example 3: Event-Driven Muzzle Flash

```mermaid
flowchart LR
    Event["Named Event Context<br/>Fire"] --> Init["Initialize Context<br/>tight cone<br/>short lifetime"]
    Init --> Update["Update Context<br/>rapid size/color curve"]
    Update --> Output["Output Context<br/>Renderer"]
    Spawn["Optional Spawn Context<br/>continuous smoke"] --> InitSmoke["Initialize smoke"]
    InitSmoke --> UpdateSmoke["Update smoke"]
    UpdateSmoke --> OutputSmoke["Output smoke"]
```

A system uses one Spawn or named Event source. When flash and smoke need different source behavior, author them as
separate systems inside the effect. Send the documented event through the emitter wrapper and keep event names stable
between gameplay and the graph.

Expected result: sending the matching event triggers the flash; merely loading the effect need not trigger an
event-only system. First validate a simple burst from Example 2, then introduce the event boundary. If no particles
appear, check the event name, emitter validity, enabled Renderer, and graph diagnostics before changing the shader.

## Example 4: Reuse With Subgraphs

```mermaid
flowchart TD
    Operator["Operator Subgraph<br/>typed value computation"] --> GraphA["Impact graph"]
    Operator --> GraphB["Ember graph"]
    Block["Block Subgraph<br/>ordered behavior"] --> GraphA
    Block --> GraphB
    System["System Subgraph<br/>complete reusable system"] --> Composite["Composite effect"]
```

Choose the explicit subgraph purpose before authoring its boundary:

- Operator Subgraph returns typed computed values through cables.
- Block Subgraph packages ordered executable Block behavior.
- System Subgraph packages a complete reusable system graph.

Kéire validates typed boundaries, dependencies, recursion, and bounded expansion. Collapsing a selection or wrapping it
in a comment does not create a subgraph. Shader Graph's function-extraction action is a separate feature, not a VFX
subgraph-creation command. This diagram explains reuse roles; it is not a promise that every role has a one-click
creation action in the Project menu.

## Compile And Performance Checklist

- Particle-stream flow is forward-only: Spawn/Event to Initialize to Update to Output.
- Every input has at most one driver and both cable endpoints have compatible `VfxValueType` values.
- Spawn systems contain enabled Emission Rate or Burst work; every system reaches an enabled Renderer.
- Portable Custom HLSL uses only the documented grammar and runs in the intended Context.
- Capacity, alive count, compute groups, CPU work, and fence latency are checked in scene and in a cooked player.
- A failed draft is repaired from the first diagnostic; the last-good preview is not mistaken for the new graph.

Use [VFX Graph](VfxGraph.md) for the authoring workflow and [VFX Authoring and Runtime](../Vfx.md) for the full current
catalog, Blocks, Context semantics, custom HLSL grammar, backend behavior, and budgets.
