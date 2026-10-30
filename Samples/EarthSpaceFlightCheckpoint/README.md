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
show the pattern and retain the ship shadow. Sparse captures do not rule out brief temporal flicker.

Remaining: cloud/Earth temporal flicker acceptance, planet-scale ground shadow artifacts,
input readiness before background refresh completes, and a portable self-contained sample.
The near-clip change is a demo mitigation, not proof of a general large-world depth fix.
