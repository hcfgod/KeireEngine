# Materials

Materials are edited in the Inspector. A Material selects a shader and stores property overrides and surface settings;
it is not a node canvas. Use Shader Graph when you need to change the shader's logic, not just its values.
This page keeps its historical filename so existing links continue to work.

## Create Your First Material

Start with an open project and a scene containing a visible mesh, a Camera, and lighting.

1. In the Project panel, choose **Create > Material**, enter `Paint`, and choose **Create**.
2. Wait for asset import to finish. The default shader is the project's pinned shared **Kéire/Lit**.
3. Double-click `Paint.keirematerial`. Its shader and reflected properties appear in the Inspector.
4. Change `BaseColor` to a visible color and `Roughness` to `0.6`. Finish the edit by releasing the control or leaving
   its field. The Inspector previews edits live and commits them at edit boundaries; there is no graph Save step.
5. Assign the Material to a Mesh Renderer material slot, then save the scene to retain that assignment.
6. Select another asset and reopen `Paint`. Confirm its values remain, then reopen the scene and check the assignment.

Expected result: the mesh uses your color. Objects sharing `Paint` share its authored values.
The preview is useful for iteration, but final lighting must be checked in the scene and a target player.

## Choose The Right Asset

| Need | Use |
| --- | --- |
| Change color, roughness, texture, or surface mode | Material Inspector |
| Change the calculation producing those values | Shader Graph, then **Create > Material from Shader** |
| Keep a persistent variant of a shared material | Select its parent, then **Create > Material Instance** |
| Change one renderer temporarily during gameplay | `MaterialPropertyBlock`, or a slot-specific dynamic material |

Select a saved shader before **Material from Shader** to preselect it. The creation dialog also provides a shader
picker. Create the Material, then assign that Material—not the `.keireshadergraph`—to its consumer.

## Properties, Surface Settings, And Recovery

Names, ranges, groups, tooltips, texture semantics, and defaults come from the selected shader. A custom shader need not
expose the same controls as Kéire/Lit. Surface shaders additionally expose **Surface Mode**, **Double Sided**, and
**Alpha Cutoff** when the mode is **Mask**. UI, Fullscreen, and VFX materials do not acquire mesh-surface controls.

Expand **Property Overrides** to reset individual explicit values. **Reset Shader Properties** restores shader defaults.
**Copy Property Values** and **Paste Compatible Property Values** preserve compatible stable property identities.
For different interfaces, **Paste Across Shaders > Paste Matching Property Names** explicitly matches shader symbols
instead. Unsupported values are skipped; ordinary paste is not a conversion between arbitrary shaders.

Changing shaders can leave **INACTIVE OVERRIDES**. These are retained values that the current shader cannot use,
not active rendering inputs. Keep them if you may switch back; **Remove Inactive Overrides** deliberately removes them.
Shader keyword changes can remain pending while the required shader loads. Resolve the diagnostic or cancel the pending
choice; the last-good preview is not proof that the new choice succeeded.

## Older Materials And Migration

Older graph-owned `.keirematerial` sources can still route to a compatibility editor. Internal types, error messages,
and historical folders may say “Material Graph”; this is not the workflow for newly created Materials.

To review conversion, leave Play Mode, finish pending asset/build work, and save or discard dirty graph drafts. Choose
**Assets > Review Legacy Material Conversion...**. Inspect the report before choosing
**Validate and Apply Reviewed Conversion**. Conversion can extract a Shader Graph and retain the Material identity;
unsupported materials retain their legacy editor. Backups remain under `Library/MaterialShaderUpgrade`. Conversion
is a project publication operation, not an ordinary Inspector undo. Keep a project backup and review diagnostics.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| A Material opens in the Inspector rather than a graph | Expected. Open its Shader Graph to edit logic. |
| Default material creation fails | Check import diagnostics and pinned shared shader inputs; do not edit generated files. |
| Property is absent | Check the selected shader and whether its parameter is exposed and imported. |
| All props change together | They share one Material; create instances for persistent differences. |
| An edit disappears after reopening | Finish the edit boundary and inspect save/import errors before closing. |
| A graph cannot be assigned to a renderer | Create and assign a Material using that shader. |
| Conversion is disabled | Leave Play Mode, finish pending work, and resolve dirty/pending graph or material state. |

Continue with [Material Examples](MaterialGraphExamples.md) and [Shader Graph](ShaderGraph.md).
