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

### Inspector-authored arm IK

For a character that needs to reach a weapon, steering wheel, ledge, control panel, or interaction point, use the
Animator's **Left Arm IK** and **Right Arm IK** groups:

1. Create or select a scene entity whose Transform represents the desired hand pose.
2. Enable the corresponding arm and assign that entity as **Target**.
3. Keep **Automatic Bone Mapping** enabled for a Humanoid or Biped rig. Kéire resolves the upper arm, lower arm, and
   hand from the imported semantic rig; the text fields are explicit fallbacks for custom naming.
4. Leave **Pole Override** empty to preserve the animated elbow bend automatically. Assign a pole entity when an
   authored elbow direction is required.
5. Use **Target Local Offset** for grip points without creating another scene object. Position and hand-rotation
   weights blend independently, so a hand can reach a target without inheriting all of its rotation.

The target and optional pole are ordinary scene references and are remapped with scene/prefab identity. The runtime
converts their world transforms at the animation boundary, clamps unreachable goals to the physical chain length, and
applies the result after managed named IK goals. A missing target, stale scene reference, incomplete semantic chain, or
non-decomposable transform produces an Animator diagnostic and leaves the sampled pose safe. This workflow complements
the generic managed two-bone/FABRIK API; it does not replace or serialize transient gameplay goals.

Each authored limb and foot grounding is evaluated independently. A missing target or invalid chain on one arm is
reported in the Animator runtime diagnostic, but it does not suppress the opposite arm, named gameplay IK goals, or
foot grounding for that frame.

## Ground Adaptation And Ragdolls

Enable **Ground Adaptation** on an Animator component. Automatic bone mapping resolves the pelvis and both leg chains
from the Humanoid/Biped semantic rig. It recognizes Mixamo, Unreal/Blender-style suffixes, 3ds Max-style side markers,
and anatomical joint names such as femur, tibia, talus, humerus, radius, and carpal. When a biped uses opaque joint
names, bind-pose topology supplies a final leg-chain fallback. The visible bone-name fields remain deterministic manual
overrides for custom, asymmetric, non-humanoid, or ambiguous skeletons; no importer-specific name is required by the IK
solver itself.

After graph sampling, managed IK, and authored arm IK, the scene runtime probes below each animated foot, ignores the
nearest Character Controller hierarchy (including a capsule on an Animator parent), rejects surfaces over
**Maximum Ground Slope**, lowers the pelvis once for the lowest valid contact, and applies a bounded support-balance
correction toward the skeleton's own bind-neutral pelvis-to-feet offset. It also removes a bounded amount of pitch/roll
from the inferred pelvis-to-chest or
pelvis-to-spine axis while preserving authored yaw. **Body Lean Correction** controls how strongly grounding removes
pitch/roll already present in the animation, and **Maximum Lean Correction** bounds that change in degrees. A zero
weight preserves the authored lean; a full weight restores the rig's bind-neutral torso direction up to the authored
angle limit. These corrections use semantic inference, skeleton topology, and the imported bind pose rather than
Mixamo names or hard-coded bone axes or strengths. The pelvis is never lifted merely to satisfy a positive sole offset.
The solver preserves each bind-pose ankle-to-sole clearance, solves both leg chains, and aligns the sampled sole normal
with the contact normal. The bind pose defines the rig's neutral sole independently of the imported foot bone's local
axes, so imported feet flatten animated toe-up pitch without folding and retain the correct clearance after rotation.
For skinned characters, clearance also includes foot- and toe-weighted bind-mesh vertices, so thick boots and armored
soles rest above the hit surface even when their visible geometry extends below every foot joint.
The authored **Sole Offset** is a minimum/fallback clearance rather than an additional lift: automatic boot thickness
replaces it when larger, preventing the two values from stacking into a visible hover. Each leg also preserves the
sampled animation's knee bend plane while reaching its vertical contact, so grounding does not pull a knee toward a
fixed model axis or distort the original forward/back stance. **Knee Stability** blends both legs toward one sagittal
bend plane derived from the rig's hip spacing, gravity, and the current sampled pose. The pole direction is transported
continuously as a contact moves across a slope, preventing a nearly straight knee from swaying, flipping, or crossing
the opposite leg. Zero preserves the sampled animation as much as possible; one strongly favors the shared stable
plane. The calculation uses semantic joints and measured transforms, not model-specific dimensions, bone names, or a
hard-coded forward axis.

Only the nearest Character Controller root and its descendants are excluded. A moving platform or other physics parent
above the character remains a valid grounding surface.

Character Controller grounding tolerates three consecutive missed walkable probes while descending or moving across
slope seams. Upward jump movement bypasses that grace immediately, so jump and fall animation state remains responsive
without flickering on ordinary ramps.

**Lock Planted Feet** holds a near-ground sole target across animation samples. When the contact belongs to a scene
entity, the target and normal are stored in that support's local space, so the planted foot and leg follow a platform
that translates, rotates, scales, or recreates its static physics body. The animation-release reference remains
independent of support motion, so moving a platform is not mistaken for a deliberate foot lift. Small horizontal motion
in an idle/walk contact phase is therefore removed instead of becoming visible skating. **Plant Distance** controls
contact acquisition; **Release Distance** releases a deliberately lifted foot, and a reach limit releases an
overextended leg so the next step can proceed. A support that travels sideways is re-anchored beneath the sampled foot
before it can pull the two-bone chain straight. If that surface leaves, the runtime immediately selects the valid
surface below and blends the visible target according to the response setting; every locked foot continues checking
the probe result, so a raised platform moved back underneath takes over from the ground lock instead of clipping
through it. Deep penetrations are recovered in the same frame. This
corrects contact-phase sliding, but it does not turn an animation without a usable gait into a complete procedural
locomotion system. **Response Time (Seconds)** smooths contact acquisition, platform movement, lower-surface handoff,
normal changes, IK weight, and release back to the sampled animation with an elapsed-time response that is independent
of frame rate. Zero selects immediate response. Upward surface motion is clamped along the contact normal in the same
frame so a smoother response cannot push the sole through an approaching platform; its lateral motion and rotation
remain filtered. Automatic toe discovery uses semantic names when present and skin influence plus bind topology
otherwise; while planted, the toe root blends back to its neutral bind rotation so the forefoot rests with the
ankle-aligned sole instead of retaining an animated upward curl.

At a ledge, one remaining planted contact also receives the bounded bind-neutral pelvis correction. This shifts the
character's weight toward the supported leg while the unsupported foot releases, rather than leaving the hips centered
between a valid foothold and empty space.

**Automatic Ray Distance** expands each downward query from the configured minimum to the evaluated leg length. This
prevents a raised animation pose from silently losing one contact and leaving a foot hovering, while the collision mask
and maximum slope keep walls and unrelated trigger geometry out of the solution. Disable it when a game deliberately
needs a strict ledge/drop cutoff. Ray height/range, sole offset, pelvis limit, plant/release distances, knee stability,
response time, lean controls, position/rotation weights, and collision mask are all serialized per Animator.

New Animators enable semantic mapping, automatic ray distance, and planted-foot locking by default. Schema-one and
schema-two Animators retain their exact authored bone-name mapping during migration, while schema-three Animators
preserve their existing semantic and limb settings; schema-four Animators retain their authored contact-lock values.
Schema-five Animators preserve their authored response and lean values and receive only the new knee-stability default.
Earlier schemas receive the defaults introduced after their version. Authors can opt into semantic mapping after
verifying a legacy custom rig.

Gameplay code can call `Animator.SetFootGroundingWeight(entity, weight)` to apply a transient `0..1` multiplier over
the authored position, rotation, and pelvis grounding weights. A zero multiplier also clears planted-foot state. Use
this at locomotion boundaries so ground adaptation remains active on slopes and moving supports but releases during
jumps, falls, swimming, climbing, or other airborne poses. This runtime value is independent of skeleton naming and is
not serialized into the Animator component.

Two-bone and FABRIK rotations are solved in model space and converted back through the actual parent transform, so
rotated parents and imported bind orientations do not corrupt local bone rotations. Two-bone chains may contain
translation, pre-rotation, and rotation helper nodes between their resolved joints, as commonly produced by FBX
importers. Semantic inference prefers the authored joint on either side of those helpers and uses a helper as a fallback
only when an authored joint is absent. Targets beyond the physical limb length are clamped without scaling bones. A
small reach margin and the persisted sampled bend plane keep knees and elbows away from folded/straight singularities
where an otherwise equivalent bend direction could flip for a frame.
Grounding reports feet that remain outside tolerance after the configured pelvis limit; malformed contacts still reject
transactionally and preserve the sampled pose.

An authored arm target's transform controls both reach and wrist orientation: **Position Weight** blends the hand
position and **Hand Rotation Weight** blends the hand bone toward the target entity's rotation. Automatic pole mapping
keeps a persistent elbow side across nearly straight or fully folded animation frames to prevent brief bend-plane flips.
Finger curl and individual finger joints are not part of limb IK; pose them in the animation/controller or with separate
managed IK goals.

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
