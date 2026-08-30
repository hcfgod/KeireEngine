# Kéire 3D Starter

This template enables the forward renderer, includes a small unlit shader, and starts with responsive retained-mode
screen and world UI Documents. Double-click `Assets/UI/StarterHud.keireui` or `Assets/UI/WorldTerminal.keireui` to open
UI Builder. The adjacent template and binding example show reusable slots, one/two-way binding declarations, a
virtualized list, and a custom-control tag. The world terminal remains depth-tested scene geometry with ray-to-UV input
and samples the frame-owned output of `StarterRenderTexture.keireui`.

Use `ScreenOverlay.keireuipanel` for HUD/menu documents and `WorldSurface.keireuipanel` for UI that belongs in the 3D
scene. Screen UI appears in Game/Play/runtime and the Builder preview, not over the Scene viewport. Builder preview
resolution, DPI, safe area, guides, and pseudo states are temporary inspection settings; edit the `.keireuipanel` asset
when the project needs a persistent presentation change. Unsaved Builder edits preview live during Play Mode and revert
when Play stops unless the document is explicitly saved.
