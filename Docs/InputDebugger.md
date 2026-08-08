# Input Debugger

The dockable Input Debugger proves the action pipeline using Kéire APIs; KeireClient contains no Dear ImGui calls or SDL
input ownership.

Opening a project attaches its default `.keireinput` asset to the editor user. Open **Window > Input Debugger** and choose
**Start Input Test**. The operation enables every map transactionally, installs RAII action subscriptions, and temporarily
bypasses UI keyboard/pointer suppression only for those test maps. Stop, panel teardown, or an initialization failure
removes subscriptions, capture overrides, and enabled maps.

Started and meaningful Performed events enter a bounded debugger-local history. Button releases are optional. Pointer,
wheel, and axis values use epsilon filtering and short-window coalescing; unchanged values, zero resets, and synthetic
end-of-frame mouse reset events create no entries. A button press therefore produces one useful record instead of a
continuous stream.

Console forwarding is off by default and must be enabled explicitly in the debugger. When enabled, each retained entry
includes:

- map/action and phase;
- processed scalar/2D value;
- user and device IDs;
- selected or automatic control scheme;
- interaction duration and monotonic input timestamp.

The debugger also lists connected/paired devices and local users. Its local history supports clear and remains isolated
from the editor Console unless forwarding is enabled.

Input Test Mode is an editor diagnostic, not gameplay routing. Runtime code should create its own `InputActionContext`,
enable only the required maps, and keep `InputCaptureOverride` scoped to deliberate tools such as rebinding or diagnostics.
