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
The final early-return correction compiles but still needs a dedicated UI acceptance pass.
Native shadow tests: 4 cases / 49 assertions. Material startup regression: 1 case / 16 assertions.

Remaining: cloud/Earth temporal flicker acceptance, planet-scale ground shadow artifacts,
input readiness before background refresh completes, and a portable self-contained sample.
The near-clip change is a demo mitigation, not proof of a general large-world depth fix.
