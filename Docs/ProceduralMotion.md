# Procedural Humanoid Motion

Kéire's `ProceduralHumanoid` Animator pose source generates a complete humanoid pose from the skeleton bind pose,
fixed-step character motion, terrain contacts, and IK. It does not load or sample animation clips. Animator Controllers
remain the default for existing components and scenes; schema-6 and older Animators migrate to `AnimationGraph`.

## Author A Profile

Create **Procedural Motion Profile** from the Project panel. The resulting `.keiremotionprofile` uses schema version 1
and stores gait, body, grounding, airborne, joint-limit, response, and normalized curve data. Select it to edit grouped
settings and the stride, lift, roll, pelvis, airborne, landing, and arm-swing curves. Invalid or non-finite settings
cannot be saved. Saving queues an isolated reimport and hot reload.

Assign the profile, a Skeleton, Skinned Mesh, and Rig Definition to an Animator, then select
`ProceduralHumanoid`. Required semantic bones are pelvis, spine, and complete left/right upper-leg, lower-leg, and foot
chains. `High`, `Medium`, and `Low` solve expensive pose/contact work every one, two, or four fixed ticks. `Auto` uses
High within 20 m, Medium within 50 m, and Low beyond 50 m while phase, state, and events continue every tick.

Do not enable the Animator's legacy automatic foot-grounding pass in procedural mode. Procedural legs own contact
planning and IK exactly once; the Inspector reports the conflicting authored setting and ignores it.

## Gameplay Contract

Submit intent during every fixed update. Desired velocity is an input, while the runtime uses the Character
Controller's actual post-physics velocity, grounded state, ground normal, and support movement for visible motion.
Facing and look directions may be zero to use the resolved character root.

```csharp
Animator.SetProceduralLocomotion(
    visual,
    new ProceduralLocomotionIntent(
        desiredWorldVelocity,
        facingWorldDirection,
        lookWorldDirection,
        crouching ? 1.0f : 0.0f,
        sprinting ? 1.0f : 0.0f,
        jumpRequested));

ProceduralLocomotionState state = Animator.GetProceduralState(visual);
```

`JumpRequested` is consumed once; other intent values persist until the next submission. The state machine reports
Idle, Locomotion, Turn In Place, Takeoff, Rising, Falling, and Landing. A confirmed jump releases both planted feet;
walking off an edge enters Falling without a Takeoff event. Unsupported legs use bounded symmetric airborne posing and
do not run ground IK.

The profile's response controls have separate ownership. Velocity response filters the realized horizontal velocity
used to build the pose without hiding the raw post-physics velocity reported in `ProceduralLocomotionState`. Facing
response filters a non-zero requested world heading and drives turn-in-place stepping plus the visual pelvis heading;
a zero facing vector continues to follow the character root. Pose response filters the completed procedural bone
solution, while grounding response filters contact acquisition, support motion, normals, IK weight, and release. All four controls use
elapsed-time exponential response, and zero selects immediate response.

While Falling, the runtime probes down from the Character Controller and ignores the character hierarchy, triggers,
masked geometry, and surfaces steeper than the profile permits. When a walkable surface is reachable within **Pre
Landing Probe Time**, the legs transition from airborne tuck into the authored falling extension before contact. No
pre-landing pose is applied over an unsupported drop or when the probe time is zero; the authoritative state remains
Falling until physics confirms contact, then Landing compression and recovery begin.

Override `OnProceduralMotionEvent` on a Behaviour attached to the animated entity to receive `FootLift`, `FootPlant`,
`Takeoff`, `Apex`, `Land`, and `StateChanged`. Foot events include side, phase, contact, support entity, and physics
material when available. Foot plants also emit the legacy `Footstep` animation event.

## Contacts And Overrides

Contact probes reuse Character Controller hierarchy filtering, so the capsule and all character descendants cannot be
selected as terrain. Planted feet track static world anchors or moving-support local anchors. The solver accepts only
walkable normals, searches backward when a stride reaches a ledge, permits independent foot heights, balances the
pelvis before two-bone IK, and never changes the root or capsule transform.

Grounding ownership follows the gait phase: a planted stance foot may hold its contact while the opposite swing foot
remains entirely under the procedural trajectory. Step travel is derived from realized speed and cadence, then bounded
by the rig's measured leg length and the profile's directional stride ratios. This keeps forward, lateral, backward,
and diagonal motion proportional without shrinking normal walking steps. If a stance foot has no reachable support at
an edge, that leg relaxes downward while the supported leg and pelvis retain balance instead of leaving the boot at its
bind-pose height.

Managed named IK and authored arm IK run after the procedural body and leg layer. External hand/look/aim constraints
therefore remain authoritative. A future external leg-chain claim can suppress the corresponding procedural plant;
climbing, mantling, swimming, weapon recoil, and ragdoll additions are outside this locomotion profile.

## Presentation Interpolation

Physics and scripts continue to read authoritative transforms. Rendering and runtime presentation systems use
interpolated `PresentationWorldMatrix()` values for Character Controllers and dynamic bodies; children combine the
presented parent with their current local transform so cameras and meshes remain synchronized.

C# exposes `Transform.PresentationPosition` and `PresentationRotation`. Call `ResetPresentationInterpolation()` after
a teleport or other discontinuous move. Scene replacement, physics-body recreation, and Play Mode initialization snap
both interpolation samples automatically. `SceneRuntimeSession::Update(delta, alpha)` accepts a render interpolation
alpha in `0..1`; the original one-argument overload remains compatible and presents the current sample.

Procedural bind, target, solved, and presentation poses are retained per Animator. Model matrices, skin palettes,
grounding request storage, and two immutable debug-snapshot buffers are also reused after dependency warm-up. A
consumer that retains both published debug snapshots receives a fresh replacement rather than allowing an older
snapshot to be mutated in place.

## Diagnostics

In Play Mode, the Animator Inspector shows the current procedural state, gait phase, actual speed, planted feet,
quality tier, and solver warning. A rig/profile/skeleton hot reload performs a controlled reset of planted contacts and
pose samples instead of blending incompatible data.
