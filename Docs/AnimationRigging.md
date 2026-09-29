# Animation And Rigging

Kéire treats model geometry, skeletons, semantic rigs, skin weights, clips, and Animator Controllers as separate assets.
This keeps reimport, retargeting, prefab references, and cooked dependencies deterministic.

## Optional Imported-Model Regression Check

The native test `downloaded rigging models preserve skeletons skinning clips and identity retargets` runs when
`Build/Validation/RiggingModels` exists. Place the Khronos glTF Sample Assets GLBs for **Fox**, **CesiumMan**, and
**RiggedSimple** in separate subfolders there, retaining their accompanying README license/credit notices. Models are
test inputs and are not redistributed with the engine. The check imports each twice, validates four/eight influences
with both skinning methods, verifies skeleton references, and retargets each clip back to its own skeleton. It skips
when the optional fixture directory is absent. Visual deformation and cross-character retargeting still require an
editor review; this check does not establish those qualities.

Additional GLBs in that directory must contain skinning and animation. The local creature pass also uses
**Wolf Spider (Rigged) - (Rabidosa rabida)** by Dreaming In Alternation 27 (CC BY 4.0), with its original credit notice.

For characters whose complete pose is generated without clips, see [Procedural Humanoid Motion](ProceduralMotion.md).
That pose source shares semantic rigs and the Animator's final override stages while leaving graph-mode behavior
unchanged.

## Import A Character

1. Import an FBX, glTF, or GLB model into the Project panel. For an animation take, set **Content** to `animation` in
   the Import Assets dialog; the source is labeled **Animation Source** and must include its embedded skinned skeleton.
2. Open **Window > Rigging Studio** and choose the model from **Model**, or select it in the Project panel.
3. Choose a **Rig Source**:
   - `Keep imported skeleton` preserves authored bones and weights.
   - `Generate a skeleton` creates a deterministic rig and weights for an unrigged mesh.
   - `None` imports static geometry only.
4. Choose the **Profile** for either an imported or generated skeleton:
   - `Humanoid` maps a conventional human skeleton.
   - `Biped` uses the two-legged profile without requiring human-specific authoring.
   - `Quadruped` maps front/rear legs, paws or hooves, spine, head, and tail.
5. Choose four or eight maximum influences and linear-blend or dual-quaternion skinning.
6. Choose an **Animation Compression** preset. `Balanced` is the default; `None`, `Light`, and `Aggressive` trade
   key count for increasingly large translation, rotation, and scale error tolerances.
7. Choose **Animation Motion**. `Root Motion` preserves extraction for animation-driven characters, `Authored` keeps
   the source pose without extraction, `In Place Horizontal` removes semantic pelvis/root X/Z travel while preserving
   vertical motion, and `In Place` also removes vertical travel for physics-driven jumps.
8. Select **Apply & Regenerate**. The isolated asset worker publishes the model and all generated subassets as one
   operation.

Embedded skeleton inference recognizes common Mixamo, Blender, and Unreal-style names. It never reorders or removes
bones. Unrecognized bones remain in the skeleton with the `None` semantic so animation and skin indices stay intact.

Unapplied import settings are retained per model while switching selection or windows within the open Studio session.
Choose **Apply & Regenerate** to publish them, or **Revert** to restore the model's current import settings. Drafts are
not saved across editor restarts.

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
to review inferred mappings. Clip and bone dropdowns reveal the current selection when opened; wheel scrolling remains
available within the list.

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

When keeping Play Mode edits, property labels include their Inspector group, such as **Right Arm IK / Enabled**,
so each limb's changes can be selected independently from the Animator component's own enabled state.

Double-click a baked `.keireanim` clip to open **Animation Clip Preview**. Select a scene object with an Animator and
skinned mesh, then use **Play**, **Pause**, **Restart**, **Stop**, or **Timeline** to inspect it. Stop scene Play Mode
first. The preview leaves the object's controller assignment and the open controller document unchanged, including
unsaved edits. **Back to Controller** restores that document. Narrow panels stack the playback buttons and wrap clip
names; the timeline uses the available panel width. Closing the panel or stopping preview clears the
temporary pose. Clips from another skeleton use the existing automatic retargeting rules; bake explicit mappings in
Rigging Studio when automatic matching is insufficient.

**Preview Speed** starts at 1× and controls only editor preview playback, even when the selected object's Animator
Speed is zero. Runtime playback still follows the object's Animator Speed. The Animator Inspector keeps a fixed runtime
status area so changing grounding or IK warnings do not move controls while they are being adjusted.

Create an **Animator Controller** in the Project panel and double-click it. Drag clips, Animation Sources, or animated
models into the graph; container assets expand their generated clip subassets into states using authored clip names.
Older clips without names use numbered model names; duplicate state names receive a numeric suffix. Create parameters, layers,
transitions, masks, blend trees, and state-machine subgraphs, then assign the controller to an Animator component.
Dropped clip batches and **Auto Layout** use a spaced grid so state titles and ports remain visible.
Override layers replace masked bones, additive layers apply deltas from the skeleton bind pose, and avatar-mask weights
can attenuate either mode per bone. Runtime sampling, events, root motion, transitions, and skinning occur in scene-safe
order.

2D blend trees evaluate only the closest triangle containing the parameter point. Parameters outside that local sample
hull project onto its nearest segment, so distant or opposing motions do not leak into the pose. Place center, walk,
and run samples on consistent contours; diagonal parameters should remain inside the contour formed by their adjacent
directional samples. Quaternion accumulation aligns equivalent rotation signs before normalization.

The state machine uses the same stable production canvas as VFX authoring:

- Drag a state's **Transition** output pin onto another state's **Enter** input pin to create a transition.
- Click a cable to inspect its duration, exit time, destination, and conditions; press Delete to unlink it.
- Exit time counts normalized state cycles: `1` permits a transition after one complete cycle, and `2` after two.
  Looping clips retain this progress across wraps and replay checkpoints; Play or entering another state resets it.
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
Use the searchable **Add Animation** picker to create states from baked clips or named imported actions without
dragging files. The first clip creates a base layer when necessary; later clips keep the existing entry state and
are added to the current layer and subgraph.
The Animation Clip, blend-child Clip, and Avatar Mask fields use searchable, type-filtered pickers. Imported actions
display their authored names beside their source model; incompatible or missing references are identified in the field.
If model placement fails during transform, renderer, or Animator setup, the partial scene object is removed.
A successful placement can be undone immediately without first clicking the Hierarchy.
Collapsing the Animator Controller or switching dock tabs keeps its Edit Mode preview running so the model stays
visible in the scene. Closing the controller, closing its document, or entering Play Mode stops the preview.
Choose **Preview Selected** to start at the selected state without changing any authored entry state. Restart and
Timeline keep that preview selection, including its layer; normal layer blending and transitions still apply.
Choose **Preview Graph** to return to the authored entry states. Stop clears the preview selection.
In Edit Mode, Preview, Pause, Restart, Stop, and Timeline scrub evaluate the graph on the selected object without
serializing the preview pose or changing the Animator's authored skeleton reference. The preview uses the skinned
mesh's skeleton locally. Invalid target assignments clear the previous pose and show a recovery message. Closing the panel, entering Play Mode, changing target, or pressing Stop clears the
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

Animator animator = GetComponent<Animator>() ??
    throw new InvalidOperationException("An Animator component is required.");
animator.Speed = 1.25f;
animator.Play("Locomotion");
animator.CrossFade("Jump", duration: 0.15f);

animator.SetFloat("Speed", velocity.Length);
animator.SetTwoBoneIK("LeftHand", "LeftUpperArm", "LeftLowerArm", "LeftHand",
                      handTarget, elbowPole, 1.0f);
animator.SetFabrikIK("SpineAim", new[] { "Pelvis", "Spine", "Chest", "Neck", "Head" },
                     lookTarget, 0.75f, maximumIterations: 12);

// Remove a persistent goal when it is no longer needed.
animator.ClearIK("LeftHand");
```

`Play` and `CrossFade` accept a controller state name, optional layer name, and normalized start time. The component also
supports `Pause`, `Resume`, and `Stop`, and reports the current state, normalized time, speed, and playback flags.
Direct `AnimationClip` and `AnimatorController` fields are supported serialized references;
explicit playback selects controller states so transitions, layers, masks, blend trees, events, and root motion remain
coherent.

IK goals persist by name until replaced or cleared. Each goal is evaluated independently in submission order;
a failed goal leaves its pose changes unapplied, reports a named diagnostic, and does not suppress later goals. World-space goals are converted to model space at the animation
boundary. Invalid entities, missing Animator components, stale Play generations, missing bones, and invalid solver
limits are rejected without exposing native pointers.
C# IK setters validate names (1..256 UTF-8 bytes), finite target/pole coordinates, weight (0..1), coordinate space,
chain length (2..256), iteration count (1..1024), and positive finite tolerance before submitting a native command.
Invalid arguments throw an `ArgumentException` naming the offending parameter and leave existing goals unchanged.
Native C++ IK setters accept only `AnimatorIkSpace::Model` and `AnimatorIkSpace::World`; other values throw
`std::invalid_argument` before adding or replacing a goal.
Bone existence and hierarchy are checked during pose evaluation, where failures produce named goal diagnostics.
Use the editor's **Pause** and **Step** controls to inspect animation and IK one frame at a time. Step runs one fixed
tick followed by Update, animation/IK evaluation, LateUpdate, VFX, and runtime UI, then remains paused.
The Animator Inspector displays these runtime diagnostics for both graph and procedural pose sources, wrapping long
messages inside the panel. Correct the reported target or bone chain; the message clears after successful evaluation.
Clear goals owned by a behaviour in its `OnDisable` callback when they should stop influencing the pose with that behaviour.

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
that translates, rotates, scales, or recreates its static physics body. Surface normals follow nonuniform and mirrored
scale changes so planted feet remain aligned with sloped surfaces. Invalid support transforms discard the anchor. Disabling or removing the support collider, deactivating its entity,
or converting it to a trigger releases the lock; restoring a solid support allows normal contact acquisition again.
Existing locks also obey the grounding collision mask, the support collider layer/mask, and the project collision
matrix, so changing filters cannot leave a foot attached to an excluded surface.
The animation-release reference remains
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

Animator component schema 7 adds the pose source, procedural profile, Rig Definition, and procedural quality fields.
Schemas 1–6 migrate to `AnimationGraph` with no procedural assets assigned, so existing playback does not change.
New Animators enable semantic mapping, automatic ray distance, and planted-foot locking by default. Schema-one and
schema-two Animators retain their exact authored bone-name mapping during migration, while schema-three Animators
preserve their existing semantic and limb settings; schema-four Animators retain their authored contact-lock values.
Schema-five Animators preserve their authored response and lean values, and all pre-knee-stability schemas receive its
current default. Earlier schemas receive the defaults introduced after their version. Authors can opt into semantic
mapping after verifying a legacy custom rig.

Gameplay code can call `Animator.SetFootGroundingWeight(entity, weight)` to apply a transient `0..1` multiplier over
the authored position, rotation, and pelvis grounding weights. A zero multiplier also clears planted-foot state. Use
this at locomotion boundaries so ground adaptation remains active on slopes and moving supports but releases during
jumps, falls, swimming, climbing, or other airborne poses. This runtime value is independent of skeleton naming and is
not serialized into the Animator component.

FABRIK can also bend a straight chain toward a closer collinear target, using the root's orientation to choose a
deterministic initial bend. Zero weight leaves the authored pose unchanged. As with other iterative targets,
`MaximumIterations` and `Tolerance` bound convergence work.
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
Contacts with zero weight do not pull or tilt the pelvis and are excluded from solved/unreachable-foot counts.
When every contact is disabled, a valid pose is preserved exactly. Invalid contact data and non-finite poses are still
rejected without modifying the input pose.
Partial contact weights also blend pelvis support: foot centers and slope normals use relative contact weights,
and the strongest contact bounds horizontal and rotational correction. Each contact's bounded vertical correction
fades with its own weight. `PelvisWeight` scales all pelvis corrections, including tilt, so fading support does not
apply a full-strength correction when its weight first becomes positive.
`RagdollPoseTransition` provides an interruptible animated-to-physics pose blend with finite duration validation,
shortest-path quaternion interpolation, zero-duration switching, and a deterministic return transition. The animation
system intentionally does not create or own a ragdoll's bodies and constraints: the physics/character layer supplies a
skeleton-compatible local ragdoll pose, keeping native body ownership outside public animation types.

## Deformation And Performance

Linear-blend skinning uses an SDL_GPU compute skin cache where compute is supported. Validated influence data is
uploaded once per asset revision, and per-entity deformation buffers are retained in a frames-in-flight ring; animation
playback uploads only the current bone palette in steady state. Dual-quaternion skinning and unsupported compute devices
use the deterministic CPU path. The deformed stream is reused by scene, depth, and shadow passes during the frame.
The CPU dual-quaternion path converts each bone matrix once per skinning call, sharing the converted palette across
vertices while rebuilding it for each pose. CPU outputs and a combined upload buffer are retained per character,
viewport, and frame-in-flight slot. Current and previous poses remain separate for motion vectors; mesh or skin
reimport retires the old buffers through the renderer fence queue.
Import settings determine whether four or eight influences are retained; weights are sorted,
bounded, and normalized deterministically.

Use four influences for crowds and distant characters. Use eight where deformation quality requires it. Use
dual-quaternion skinning for twisting joints that visibly lose volume under linear blending, and profile the target
hardware before applying it broadly.

## Cooking Guarantees

Rigging Studio's **Mapping Profile** selects semantic inference rules; it does not replace an imported skeleton.
Custom creatures retain their full hierarchy and animation even when no semantic chains can be inferred. Inspect the
bone map and use explicit bone names for custom IK. Quadruped inference also recognizes front limbs named UpperArm,
ForeArm and Hand, and rear limbs named Leg01, Leg02 and Foot.

Clip choices and generated assets show authored names after reimport. Partial retargets show a warning and require
**I reviewed the omitted tracks** before baking. Changing source or target data resets that review.
Use **Edit bone mappings** to select explicit target bones for unmatched or incorrectly matched source tracks.
Filter source bones or search within a target picker to locate joints in large rigs. Automatic fields show the
resolved target name, or **no matching target**, so missing mappings are visible without opening diagnostics.
**Automatic** restores name/semantic matching for that track. Manual choices take priority over automatic matches;
assigning the same target to multiple manual source bindings is rejected. Overrides and searches survive asset
refreshes, including saving another animation graph. Reloaded rigs are validated again; removed or renamed bones
produce an error until their mappings are repaired. Selecting another source clip or target model resets the draft.
Bake the clip to retain the resulting animation; mapping drafts are not saved across editor restarts.

The C++ diagnostic and bake functions also accept a span of `AnimationRetargetOverride` entries containing
`SourceBone` and `TargetBone` names. They reject unknown names and duplicate logical source/target bindings before
baking. Existing overloads retain automatic matching. Diagnostics identify manual matches separately.

Apply or revert pending import settings first; failed source or target imports keep their diagnostics visible and cannot be used for a new bake.
Repair and reimport the affected model even if its last good cached clip or preview remains available.

Dragging a skinned model from Project into the Scene creates a Mesh Renderer and an Animator with its imported
skeleton, skin, and rig assigned. Create or assign an Animator Controller to select and play its clips. Static
models remain renderer-only. Ambiguous skins or a missing skeleton produce a reimport diagnostic before placement.
Clean Inspector fields follow edits from Rigging Studio; conflicting drafts are retained with a warning, and Revert
loads the latest imported settings.

Downloaded-model regression coverage also carries solved poses through 24 successive moving targets per eligible
three-bone chain, checking finite transforms, anchored roots, segment lengths, and progress toward each target for
both two-bone and FABRIK solvers. This complements, but does not replace, live gameplay and deformation review.
The optional spider regression solves all eight complete leg chains over 48 moving-target frames, checks isolation
between legs, preserved segment lengths, unreachable targets, and recovery after invalid input. Zero-weight FABRIK
preserves the input transforms exactly, while still validating the request and chain.
Managed runtime integration coverage loads a gameplay assembly into a playing scene and verifies repeated public
`Animator` IK updates, unchanged goals after rejected arguments, `OnDisable` cleanup, re-enabling, and rejection through
a cached component reference after deferred removal completes. These checks cover command and lifecycle behavior;
they do not substitute for visual review of the resulting pose on each rig.

IK rotation extraction normalizes basis lengths before decomposition, so unit conversion scales such as 0.001 do not
make valid imported chains fail. Optional downloaded-model tests exercise both two-bone and FABRIK solvers on
nonzero-length three-bone chains, checking finite poses, stationary roots, and progress toward targets. These numerical
checks supplement visual testing; they do not certify deformation quality or automatic semantic mapping for every rig.

Strict cooking rejects:

- malformed rig, clip, skeleton, or skinned-mesh payloads;
- clips whose skeleton is absent, incorrectly typed, or undeclared as a dependency;
- skinned meshes whose mesh or skeleton dependency is absent or incorrectly typed;
- influence arrays whose vertex count differs from the source mesh;
- positive-weight bone indices outside the referenced skeleton.

Generated IDs and schema-v1 skinned meshes remain compatible. Reimport writes immutable cache generations and only
publishes a complete last-good result.

Custom/imported mapping preserves authored bone names and hierarchy without inferring humanoid roles. Use it for spiders and other custom creatures; address IK chains by their bone names. Generating a custom skeleton requires an authored profile through the C++ API; the importer offers generation only for humanoid, biped, and quadruped profiles.

In **Edit bone mappings**, **Save Mapping** writes the manual pairs to `Config/RetargetMappings` in the project.
The file is keyed by stable source/target skeleton IDs, so other clips from that pair can use **Load Saved Mapping**,
even after restarting the editor. Include these configuration files in project version control when sharing mappings.
Save replaces the previous mapping for that pair; Load replaces the current manual edits. Loading validates the file,
identities, unique bindings, and current bone names before replacing anything. Missing or renamed bones produce a
repair message and leave the current edits unchanged. An empty saved mapping restores automatic matching.
Saving also checks the current skeletons before writing: stale bone pairs cannot overwrite an existing valid preset.
Mappings do not copy automatically to unrelated skeleton assets; review and save their bindings separately.

Two-bone and FABRIK solves are transactional: a rejected request or invalid pose leaves every input transform unchanged. Non-finite transforms and zero-length rotation quaternions are rejected.

Root-motion extraction retains the skeleton root's bind rotation and translation in the rendered pose.
This preserves imported axis corrections and model origins; initial movement is measured relative to
the bind translation, and subsequent movement uses consecutive sampled root positions.

Animator runtime diagnostics refresh on each evaluation. Correcting a missing bone or disabling the
failing grounding pass clears its warning on the next update; unresolved failures remain visible.
