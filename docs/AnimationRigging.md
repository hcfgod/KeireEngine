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
6. Select **Apply & Regenerate**. The isolated asset worker publishes the model and all generated subassets as one
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

Retargeting matches semantic roles rather than type or bone names. The baked `.keireanim` clip references the target
skeleton and can be dragged into an Animator Controller. Missing optional roles are skipped; source and target assets
remain unchanged if validation fails.

## Animator Controllers

Create an **Animator Controller** in the Project panel and double-click it. Drag clips, Animation Sources, or animated
models into the graph; container assets expand their generated clip subassets into states. Create parameters, layers,
transitions, masks, and blend trees, then assign the controller to an Animator component. Runtime sampling,
events, root motion, transitions, and skinning occur in scene-safe order.

Managed gameplay code controls typed parameters and named IK goals:

```csharp
using Keire;

Animator.SetFloat(Entity, "Speed", velocity.Length);
Animator.SetTwoBoneIK(Entity, "LeftHand", "LeftUpperArm", "LeftLowerArm", "LeftHand",
                      handTarget, elbowPole, 1.0f);
Animator.SetFabrikIK(Entity, "SpineAim",
                     new[] { "Pelvis", "Spine", "Chest", "Neck", "Head" },
                     lookTarget, 0.75f, maximumIterations: 12);

// Remove a persistent goal when it is no longer needed.
Animator.ClearIK(Entity, "LeftHand");
```

IK goals persist by name until replaced or cleared. World-space goals are converted to model space at the animation
boundary. Invalid entities, missing Animator components, stale Play generations, missing bones, and invalid solver
limits are rejected without exposing native pointers.

## Deformation And Performance

Linear-blend skinning uses an SDL_GPU compute skin cache where compute is supported. Dual-quaternion skinning and
unsupported compute devices use the deterministic CPU path. The deformed stream is reused by scene, depth, and shadow
passes during the frame. Import settings determine whether four or eight influences are retained; weights are sorted,
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
