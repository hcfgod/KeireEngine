# Animation And Rigging

Kéire treats model geometry, skeletons, semantic rigs, skin weights, clips, and Animator Controllers as separate assets.
This keeps reimport, retargeting, prefab references, and cooked dependencies deterministic.

## Import A Character

1. Import an FBX, glTF, or GLB model into the Project panel. For an animation take, set **Content** to `animation` in
   the Import Assets dialog; the source is labeled **Animation Source** and must include its embedded skinned skeleton.
2. Select the model and open **Window > Rigging Studio**.
3. Choose a **Rig Source**:
   - `Embedded` preserves authored bones and weights.
   - `Generate` creates a deterministic rig and weights for an unrigged mesh.
   - `None` imports static geometry only.
4. Choose the **Avatar Profile**:
   - `Humanoid` maps a conventional human skeleton.
   - `Biped` uses the two-legged profile without requiring human-specific authoring.
   - `Quadruped` maps front/rear legs, paws or hooves, spine, head, and tail.
5. Choose four or eight maximum influences and linear-blend or dual-quaternion skinning.
6. Choose an **Animation Compression** preset. `Balanced` is the default; `None`, `Light`, and `Aggressive` trade
   key count for increasingly large translation, rotation, and scale error tolerances.
7. Select **Apply & Regenerate**. The isolated asset worker publishes the model and all generated subassets as one
   operation.

Embedded skeleton inference recognizes common Mixamo, Blender, and Unreal-style names. It never reorders or removes
bones. Unrecognized bones remain in the skeleton with the `None` semantic so animation and skin indices stay intact.

Arbitrary creatures use `RigProfileType::Custom` and an authored `RigDefinition` through the public C++ API. A custom
profile defines exact bones, parentage, bind transforms, semantic roles, and IK chains:

```cpp
Keire::AutoRigRequest request;
request.Profile = Keire::RigProfileType::Custom;
request.CustomProfile = authoredRig;
request.Skinning = Keire::SkinningMethod::DualQuaternion;
request.MaximumInfluences = 8;
const auto result = Keire::GenerateRig(mesh, request);
```

## Inspect And Retarget

Rigging Studio lists the model's skeleton, semantic rig, skinned mesh, and embedded clips. Expand **Semantic Bone Map**
to inspect the durable role assigned to each source bone.

To retarget:

1. Select the target model in Rigging Studio.
2. Expand **Animation Retargeting**.
3. Choose a source clip from another imported model.
4. Enter a destination name and create the retargeted clip.

Before baking, Rigging Studio reports exact-name, semantic, unmapped, and conflicting bone matches; root-motion
compatibility; translation scale; and any bones that need a scale fallback. Incompatible root motion disables the bake
instead of creating a clip with an invalid root track. The mapping table remains available as a retarget preview so a
content author can correct the rig definitions before writing an asset.

Retargeting matches exact bones first and semantic roles second. The baked `.keireanim` clip references the target
skeleton and can be dragged into an Animator Controller. Missing optional roles are skipped; pathological scale ratios
fall back to a safe value; source and target assets remain unchanged if validation or the final bake fails.

## Animator Controllers

Create an **Animator Controller** in the Project panel and double-click it. Drag clips, Animation Sources, or animated
models into the graph; container assets expand their generated clip subassets into states. Create parameters, layers,
transitions, masks, blend trees, and state-machine subgraphs, then assign the controller to an Animator component.
Override layers replace masked bones, additive layers apply deltas from the skeleton bind pose, and avatar-mask weights
can attenuate either mode per bone. Runtime sampling, events, root motion, transitions, and skinning occur in scene-safe
order.

The state machine uses the same stable production canvas as VFX authoring:

- Drag a state's **Transition** output pin onto another state's **Enter** input pin to create a transition.
- Click a cable to inspect its duration, exit time, destination, and conditions; press Delete to unlink it.
- Drag a state card to move it. One completed gesture produces one undoable layout edit.
- Right-click a state to make it the entry state, unlink its outgoing transitions, or delete it.
- Right-click an input pin to unlink incoming transitions, or right-click a cable to delete that exact transition.
- Middle-drag to pan, use the wheel to zoom, and choose **Frame All** after a large layout change.
- Drop clips at the intended graph position. Multi-clip drops are offset so newly created states remain selectable.
- Select the root state machine or a named subgraph in the navigation tree. Each group owns its own entry state while
  stable-ID transitions may cross group boundaries.

Self-transitions are rejected. A second transition between the same two states is allowed with a warning because its
conditions or exit timing can be distinct. Deleting a state also removes every incident transition transactionally.

Select the animated scene object while its controller is open to use the selection-backed **Animation Preview Scene**.
In Edit Mode, Preview, Pause, Restart, Stop, and Timeline scrub evaluate the graph on the selected object without
serializing the preview pose. Closing the panel, entering Play Mode, changing target, or pressing Stop clears the
transient pose. In Play Mode, the same strip reports the live state and normalized progress, displays the active
transition and blend progress, and highlights the active graph state.

The preview and live strips expose three bounded debug views: the final local pose and derived model-space bone
positions, a 240-sample accumulated root-motion trajectory, and state-machine timings/counters for layers, transition
tests, motions, and sampled clips. Debug data is published through immutable snapshots, so inspecting it cannot mutate
or stall graph evaluation. The skinned mesh is authoritative for the target skeleton; source clips from another
compatible rig are retargeted to that skeleton. Embedded imports rebuild inverse binds from the normalized runtime
hierarchy, and retargeting discards pathological unit-conversion scale ratios instead of allowing them to corrupt the
skin palette.

Managed gameplay code controls typed parameters and named IK goals:

```csharp
using Keire;

AnimatorHandle animator = Entity.Animator;
animator.Speed = 1.25f;
animator.Play("Locomotion");
animator.CrossFade("Jump", duration: 0.15f);

Animator.SetFloat(Entity, "Speed", velocity.Length);
Animator.SetTwoBoneIK(Entity, "LeftHand", "LeftUpperArm", "LeftLowerArm", "LeftHand",
                      handTarget, elbowPole, 1.0f);
Animator.SetFabrikIK(Entity, "SpineAim",
                     new[] { "Pelvis", "Spine", "Chest", "Neck", "Head" },
                     lookTarget, 0.75f, maximumIterations: 12);

// Remove a persistent goal when it is no longer needed.
Animator.ClearIK(Entity, "LeftHand");
```

`Play` and `CrossFade` accept a controller state name, optional layer name, and normalized start time. The handle also
supports `Pause`, `Resume`, and `Stop`, and reports the current state, normalized time, speed, and playback flags.
`AssetReference<AnimationClip>` and `AssetReference<AnimatorController>` fields are supported serialized references;
explicit playback selects controller states so transitions, layers, masks, blend trees, events, and root motion remain
coherent.

IK goals persist by name until replaced or cleared. World-space goals are converted to model space at the animation
boundary. Invalid entities, missing Animator components, stale Play generations, missing bones, and invalid solver
limits are rejected without exposing native pointers.

## Ground Adaptation And Ragdolls

Enable **Ground Adaptation** on an Animator component and assign pelvis, upper-leg, lower-leg, and foot bone names for
both legs. After graph sampling and explicit IK goals, the scene runtime raycasts below each foot, ignores the
character's own physics body, lowers or raises the pelvis within the configured limit, solves both two-bone chains, and
aligns the feet to the hit normals. Collision mask, ray height/range, foot offset, positional weight, and rotation
weight are serialized settings. If either chain or contact is invalid, the operation rejects transactionally and the
sampled pose remains unchanged.

`SolveFootGrounding` is also available to custom character runtimes that already own their contact queries.
`RagdollPoseTransition` provides an interruptible animated-to-physics pose blend with finite duration validation,
shortest-path quaternion interpolation, zero-duration switching, and a deterministic return transition. The animation
system intentionally does not create or own a ragdoll's bodies and constraints: the physics/character layer supplies a
skeleton-compatible local ragdoll pose, keeping native body ownership outside public animation types.

## Deformation And Performance

Linear-blend skinning uses an SDL_GPU compute skin cache where compute is supported. Validated influence data is
uploaded once per asset revision, and per-entity deformation buffers are retained in a frames-in-flight ring; animation
playback uploads only the current bone palette in steady state. Dual-quaternion skinning and unsupported compute devices
use the deterministic CPU path. The deformed stream is reused by scene, depth, and shadow passes during the frame.
Import settings determine whether four or eight influences are retained; weights are sorted,
bounded, and normalized deterministically.

Use four influences for crowds and distant characters. Use eight where deformation quality requires it. Use
dual-quaternion skinning for twisting joints that visibly lose volume under linear blending, and profile the target
hardware before applying it broadly.

## Cooking Guarantees

Strict cooking rejects:

- malformed rig, clip, skeleton, or skinned-mesh payloads;
- clips whose skeleton is absent, incorrectly typed, or undeclared as a dependency;
- skinned meshes whose mesh or skeleton dependency is absent or incorrectly typed;
- influence arrays whose vertex count differs from the source mesh;
- positive-weight bone indices outside the referenced skeleton.

Generated IDs and schema-v1 skinned meshes remain compatible. Reimport writes immutable cache generations and only
publishes a complete last-good result.
