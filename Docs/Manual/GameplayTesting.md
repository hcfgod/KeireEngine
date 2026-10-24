# Test Gameplay From Edit To Player

Use this pass after a small playable feature works and before handing a build to someone else. It covers the places
where a script that compiles can still behave differently: input focus, scene references, reload, saved state, and
cooked content. Keep a short record of the scene, target profile, steps, expected result, and observed result.

## Prepare A Known Starting Point

Save your scene and authored assets. Wait for imports and the latest managed build. Check the Console for compile,
type-discovery, migration, and callback errors. Last-good scripts can keep running after a failed edit; verify the
newest generation before attributing behavior to the visible source code.

Choose a single scene and one feature to test first. Note which input asset, prefab, UI document, audio clip, and
destination scene it requires. Inspector references should identify the assets rather than gameplay code guessing
project paths. Use a separate test scene when exercising destructive gameplay such as destroying a hierarchy.

## Pass 1: Ordinary Player Actions

1. Enter Play and focus Game view. Exercise every intended input path: pointer, keyboard, and gamepad where configured.
2. Move diagonally and against collision surfaces. Confirm normalized movement, fixed-step motion, and the intended
   behavior when several simulation ticks occur in one rendered frame.
3. Trigger the feature repeatedly and rapidly. A pickup should grant once if designed that way; a scene portal should
   not queue duplicate loads; a sound/effect failure should follow an intentional fallback policy.
4. Open and close menus. Confirm cursor capture/visibility, initial focus, Submit, and return to gameplay.
5. Pause, resume, and change time scale if your game supports them. Scaled timers, delayed destruction, and fixed-step
   motion should follow that scale; explicitly unscaled presentation should follow its own documented policy.
6. Resize the window and check UI layout, readable text, and hit targets. Check the real Game view as well as Builder.

Expected result: each input produces the intended action once, visible state matches gameplay, and the Console stays
free of unexplained errors. A pressed input edge belongs to the frame snapshot; consume/latch one-shot physics intent
once rather than applying it on every fixed tick in that frame.

## Pass 2: Lifetime And Reload

Disable and re-enable the owning component, then its entity. Destroy an optional target and exercise the feature again.
Unassigned references are `null`; destroyed wrappers can remain non-null with `IsValid == false`. Both cases need an
intentional behavior.

While playing, make a harmless script change and save. Check successful compilation and reload, then repeat the action.
Subscriptions, cursor tokens, input contexts, jobs, and asset leases must not duplicate or remain owned by the retired
generation. `Awake` and `Start` do not rerun after reload; rebind runtime relationships in active `OnAfterReload` paths.

For async features, disable the owner while a load/job/path operation is pending. Its lifetime-bound work should cancel
or stop publishing results. Validate entities again after an `await`; returning to the owner thread does not keep the
target alive. Restore the owner and verify a fresh request works.

For authored UI, change and save a document while playing, then click it again. Live element handles belong to a
document generation; resolve them again after reload. A rejected draft should show diagnostics while the last usable
document remains available.

## Pass 3: Stop And Reopen

Request Stop. If **Play Mode Changes** opens, inspect the before/after values and origin labels. Editor changes are
selected by default; simulation-only changes are not. Use **Apply Selected and Stop** only for the changes you want to
keep, or **Discard and Stop** for the session. **Cancel** returns you to Play. Locked or conflicting entries require
reading their explanation rather than assuming every runtime edit can be applied.

Apply updates the edit scene; save it afterward. Source saves are separate: a saved script, UI document, or stylesheet
is already on disk even when scene changes are discarded. Reopen the scene or project and verify only the intended
authored state remains. This is also the moment to check for stale references after removing starter content.

## Pass 4: Build And Run

Follow [Player Builds](../PlayerBuilds.md) with your intended target profile and startup scene. Include every runtime
destination scene in the ordered build scene set. Keep runtime gameplay out of Editor-classified assemblies.

Launch the built player from its output folder. Repeat movement, UI, audio, prefab spawning, scene transitions, pause,
and exit. Test transitions from the actual startup scene; an isolated scene preview does not prove its dependencies
are cooked or the previous scene's owners tear down correctly. Preserve the output folder structure when sharing a
build because the executable is only one part of the player.

If a feature works in Play but fails in the player, start with startup-scene selection, asset dependency closure,
runtime assembly inclusion, and platform/backend support. Record the first diagnostic before retrying unrelated fixes.

## Capture A Useful Bug Report

Include reproducible steps, expected/actual behavior, the Editor/player build identity, target platform, and the first
relevant Console/runtime error. State whether it reproduces after reopening the project and in a packaged player.
Use [Debugging and Profiling](DebuggingAndProfiling.md) for captures and diagnostic bundles. Keep player data and
secrets out of a report; diagnostic collection is a local operation until you choose to share the bundle.
