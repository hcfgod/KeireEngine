# Editor authoring UI pass

Planetary, water, and post-processing rendering development is on hold while this authoring pass is active.
Preserve its existing work and external integration drafts. Use master and batch changes before DevDist builds.

## Required work

- Run the existing editor checkpoint through real mouse/keyboard workflows.
- Replace repeated effect-state labels with unobtrusive, discoverable state information.
- Reorganize Environment into focused navigation and a settings inspector, with responsive narrow-panel behavior.
  Preserve time preview, timelines, Undo/Redo, saving, and all existing controls.
- Reveal Scene selections in the Hierarchy: expand ancestors, highlight and scroll to the selected entity without
  repeatedly overriding deliberate manual collapse. Handle filtering, multi-selection, and scene switches.
- Improve Hierarchy dragging and reordering: clear insertion lines and parenting targets, stable multi-selection,
  visible-order range selection, useful invalid-drop feedback, and predictable root drops. Preserve world transforms
  and Undo/Redo. Validate nested parents and filtered results.
- Add discoverable center handles for screen-plane movement, free rotation, and uniform scaling. Preserve axis handles,
  local/world space, snapping, multi-object transforms, cancel and Undo/Redo, including far-world positions.
- Audit remaining panels for clutter, unclear labels, narrow layouts, search, keyboard navigation, tooltips,
  consistent spacing, error recovery, and unnecessary steps. Record and fix verified issues in related batches.

## Evidence and progress

- Baseline checkpoint: 32 core and 29 post-processing editor tests passed; DevDist staged successfully.
- Reproduced Scene selection visibility failure: selecting Studio Paint updates Inspector and gizmo but the Material
  Lab parent stays collapsed in Hierarchy. Selection exists; automatic reveal is missing.
- Source inspection: only Scale has a center handle; Move and Rotate do not.
- Effect list label cleanup implemented and visually checked; an additional row-spacing adjustment addresses user feedback.

## Acceptance

Use a preserved QA project. Check each changed workflow with real input at wide and narrow panel widths; check
keyboard/mouse selection, drag/drop, save/reopen, undo/redo, canceled gestures, and no accidental scene edits.
Run focused regression tests and formatting checks. UI polish is not evidence that rendering features are complete.

## Bounded batch for the next pause

Implemented and built in DevDist; focused regression and checkpoint interaction results are recorded below:

1. Inline effect-state badges replace repeated secondary label lines.
2. Effect-state tooltips explain inherited/base values and authored overrides.
3. Environment uses navigation plus a focused settings inspector.
4. Sky colors, atmosphere, planets, and stars have separate focused sections.
5. Layered and volumetric clouds have separate sections.
6. Narrow Environment panels use a compact section selector.
7. Environment exposes Undo and Redo beside Save.
8. Preview time shortcuts use a compact transport dropdown.
9. The clock slider uses hours rather than an unexplained fractional day.
10. Preview day length no longer displays unnecessary decimal tails.
11. Selecting in Scene reveals and scrolls to the Hierarchy row.
12. Leaf entities no longer display misleading expansion arrows.
13. Hierarchy search has a clear button.
14. External selection clears a Hierarchy filter that could hide it.
15. Shift selection follows visible tree rows rather than hidden descendants.
16. Dragging an unselected row selects the object being moved.
17. Clicking empty Hierarchy space clears selection.
18. Hierarchy gesture instructions move to a tooltip to reduce clutter.
19. Move and Rotate have center handles, with explanatory hover tooltips; uniform Scale remains available.
20. Returning a transform drag to its start restores the initial transform.
21. Prefab/mesh/VFX Scene drops prefer visible mesh bounds instead of always projecting to global ground.

Drop placement currently uses oriented mesh bounds, not triangle-exact surface queries. Verify the actual reported
prefab drop as recorded below. Do not claim this bounded pass finishes the entire editor UI overhaul.

## Checkpoint validation (2026-11-01)

- DevDist editor staging passed. Focused editor regression: 160 cases / 22,763 assertions passed.
- Shared tree input regression: 2 cases / 23 assertions passed.
- Follow-up prefab/drop regression after fixing serialized Transform position: 16 cases / 111 assertions passed
  (overlaps the earlier suite; these counts are not additive).
- Real editor input confirmed Scene selection expands ancestors and reveals the selected Hierarchy row.
- Center Move and free Rotate changed the selected entity and Undo restored its baseline transform.
- Environment section navigation and hours-based preview controls were visually checked at the normal wide layout.
- Post Processing displays compact inline Base badges without secondary inherited-label lines.
- Dragging PhysicsCrate onto the showcase placed its pivot at the mouse location, approximately
  (-4.9295, -0.625, 0.837142), rather than its authored (0, 1, 0). Undo returned the scene to 25 entities.
- Hierarchy drag selected the moved entity; reparenting retained its visible world position and Undo restored the parent.
- Test mutations were undone; no scene/profile test edits were saved.
- Follow-up from user: add vertical breathing room between badge rows. Rebuilt visual check is pending.

## Remaining after this bounded batch

- Finish Unity-like Hierarchy drag affordances: clearer before/after versus child targets, hover expansion,
  edge auto-scroll, invalid-drop feedback, and nested/filtered multi-selection acceptance.
- Expand live QA to narrow layouts, search clearing, Shift ranges, snapping/cancel, uniform scale, and Environment
  Undo/Redo/save/reopen. These are not covered by the checkpoint visual claims above.
- Continue the wider panel audit when resumed. Planetary, water, and post-processing rendering work remains paused.

### Additional user-requested Hierarchy batch

The user explicitly extended this checkpoint to implement the remaining drag work before pausing. Source now includes
larger insertion zones and endpoint markers, ancestry validation and red invalid-drop feedback, delayed hover expansion,
vertical edge scrolling within a dedicated entity list, and shared drag handling for filtered results (parenting only).
Focused tests cover drop zones, stable-hover timing, cycle/self/selected-anchor rejection, and scroll API validation.
Rebuild and editor interaction acceptance are pending; earlier remaining-work entries are superseded only after validation.

Additional hierarchy regression result: KeireEditorTests focused selection passed 99 cases and 1,189 assertions.
The new hierarchy policy tests cover self/descendant cycles, stale destination IDs, selected insertion anchors,
normal versus filtered drop zones, and hover timing/reset. Live long-hover expansion and sustained edge scrolling
still need explicit interaction acceptance; do not infer them from a successful compilation.

## Pause checkpoint: final rebuilt editor

- Final DevDist staging passed (hierarchy-final-stage.log); no shipping package or website publication was attempted.
- Shared UI tests passed 2 cases / 33 assertions after adding auto-scroll argument validation.
- Live sibling reorder passed: Tiled Ceramic moved before Studio Paint at the top-edge target while its transform
  remained (-2.5, 1.15, -5), scale 1.55. Undo restored the original order and cleared the scene dirty flag.
- Attempting to drop Foundations onto its own child left the hierarchy unchanged and the scene clean.
- Scene selection reveal still works inside the new child scroll region.
- The additional badge spacing is visually confirmed; status pills are separated vertically.
- Scene and profile files matched the preserved baseline before the final live checks. Final live mutations were undone.
- Long-hover expansion, sustained edge scrolling, filtered multi-selection and narrow-layout acceptance remain pending.
  Implementation and focused rule tests passed; do not claim full Unity parity or a finished editor-wide UI overhaul.
- Stop at this checkpoint per the user's requested pause. Resume only when asked.

## 2026-11-02 follow-up validation

- Added a headless, multi-frame input regression for the actual UI child scrolling implementation: held pointer at the bottom scrolls down, moving to the top scrolls up, and releasing stops scrolling. The fixture disables window movement to match the docked Hierarchy.
- Incremental KeireTests DevDist build passed (two steps). UiSectionInputTests: 3 cases / 39 assertions passed. Existing hierarchy hover-delay/reset and editor regressions: 99 cases / 1,189 assertions passed.
- Reopened the staged editor maximized. Environment navigation layout and viewport-to-Hierarchy selection/reveal were visually confirmed. No scene edits were made.
- No production code changes were needed in this follow-up. The staged editor remains current; no installer rebuild or publication was performed.
- Sustained real desktop hover/edge dragging remains a manual acceptance limitation: the supported computer-use drag API has no hold duration. Hover timing/reset has deterministic coverage and actual scrolling has multi-frame input coverage; these are not claimed as a live held-drag test.
