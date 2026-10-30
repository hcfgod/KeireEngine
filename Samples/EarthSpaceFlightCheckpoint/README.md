# Earth-to-space flight acceptance checkpoint

The runnable local QA project is `D:/Projects/KeireValidation/EarthSpaceFlight`.
This directory preserves authored source and settings, not an independently packaged project.
It requires the shared input, material and scripting assets from the CloudIsolation QA project;
no asset caches or generated binaries are included. Do not overwrite the original QA project.

Open the local project, wait for input/script imports, then press Play and click Launch.
The 150-second route ascends to 150 km over 60 seconds, coasts for 20 seconds, descends
for 60 seconds and remains landed for 10 seconds. Enter launches, V switches the
cockpit/follow camera, P pauses/resumes, and R begins return. Buttons provide the same actions.
The return command mirrors an unfinished ascent onto the descent curve to preserve altitude.
Camera position and pitch blend exponentially. The near clip grows from 0.2 to 10 metres
by 5 km altitude to mitigate depth precision loss while retaining the follow-view ship.

This is a scripted environment test using precise coordinates, a geometric ship and a simple
Earth sphere. It is not a rocket physics simulation or finished planetary terrain system.
Atmosphere, volumetric clouds, height fog, stars and environment timelines are configured.

Verified October 29: original showcase materials restored (also confirmed by the user),
150-second round trip, launch/pause buttons, cockpit/follow buttons in both directions,
stars and atmospheric limb at altitude. Script compilation passes without warnings.
The final early-return correction was exercised during ascent: the route switched to descent,
returned to the landed state, and both camera buttons worked during descent.
Native shadow tests: 5 cases / 57 assertions. Material startup regression: 1 case / 16 assertions.
UI isolation identified Earth self-shadowing as a source of the ground pattern: disabling only
Earth's shadow casting removed it while retaining the ship shadow. The anchored depth-transform
fix passes native regressions. Rebuilt ground and landed captures with casting restored no longer
show the pattern and retain the ship shadow. The user subsequently watched the flight and confirmed no flicker on October 29. This accepts this route and configuration; it does not establish every altitude, planet or graphics backend.
The Environment panel's scattering fields were visually verified in the rebuilt editor as decimal
values (for example, 0.000005802). UI regressions pass: 15 cases / 413 assertions.

Remaining: broader planet-scale shadow and cloud coverage across configurations,
input readiness before background refresh completes, and a portable self-contained sample.
The near-clip change is a demo mitigation, not proof of a general large-world depth fix.

## Next acceptance and performance work

Prioritize measured cold/warm startup and asset readiness, then CPU/GPU frame-time captures
for stationary ground, ascent, orbit and landing with identical viewport and quality settings.
Report median, p95 and worst frame times separately; do not infer performance from screenshots.
Optimize the measured bottleneck and compare the same route without reducing accepted visual quality.

Production acceptance still includes multiple-planet selection/composition, rotating physics-frame
transfers, far-origin physics under sustained workloads, save/reload and standalone player parity,
backend coverage and long-session stability. The scripted flight is one acceptance scenario.

Player preparation: BuildScenes now includes only the flight startup scene (the inherited sandbox list
omitted it). Player settings identify Earth Space Flight. Original external settings are backed up under
ValidationBackups/BeforeFlightPlayerSettings. Project validation passes; a newly built standalone executable
and its full flight still require acceptance. Export/import is deferred until native compilation completes
to avoid recreating the shader compiler contention seen earlier.
