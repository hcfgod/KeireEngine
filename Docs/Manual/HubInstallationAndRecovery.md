# Hub Installation And Recovery

Use Hub to manage Editor installations and projects. Local project creation and editing do not require a Marketplace
account. Browser sign-in, online catalogs, and private asset downloads are separate network-dependent workflows.
These instructions describe the current source implementation; an older installed setup may not expose every action.

## Install Or Maintain The Windows Hub

Download the intended Windows setup from the official Downloads page and close the running Hub before maintenance.
When setup detects an existing registered Hub, choose explicitly:

| Setup choice | Result and limits |
| --- | --- |
| **Repair / update this installation** | Reuses the registered root. Missing owned payload files can be restored. Modified owned files are rejected rather than silently overwritten. |
| **Uninstall the Hub** | Removes installer-owned content and registration; projects and unrelated files are preserved. |
| **Install in a new location (remove the current Hub first)** | Validates the new destination and setup payload, removes the old Hub, then installs at the new root. This is not an atomic move. |

For repair, retain the ownership metadata and registration. If a modified file blocks repair, back up the identified
file and follow the diagnostic; do not delete ownership receipts or disable validation. Missing ownership metadata is
not equivalent to a missing executable and cannot be repaired by guessing which files setup owns.

For relocation, select a new empty destination outside the old installation tree. Preflight rejects nested roots,
unsafe path links, conflicting contents, and invalid payloads before removal. It cannot guarantee that later disk,
permission, or process conditions will remain unchanged. If installation fails after old-Hub removal, rerun setup
using the reported destination. Preserve diagnostics and any unrelated files left in the previous root.

Do not manually copy the Hub directory and assume shortcuts or the `keirehub://` browser association moved with it.
Maintenance owns registration changes. The Hub installer is separate from the Editor **Repair** action described below.

## Install Or Register An Editor

1. Open **Installs** and choose the desired catalog release.
2. Review the Editor version, platform/architecture, components, download size, and disk requirement.
3. Choose an absolute install location and review the plan before **Install editor**.
4. Wait for installation and verification to finish. Confirm the installation is healthy before creating a project.

Missing ordinary parent directories for a custom location are created when installation begins. A drive root, missing
drive, existing file, linked/reparse path, or conflicting installation is not made safe merely by typing it into the
location field. Read the validation message; choose an ordinary local directory with sufficient access and space.

Use **Locate existing** for an Editor already on disk. This is registration, not copying or adopting arbitrary files
into managed ownership. An externally rebuilt Editor may need **Refresh registration** after an intentional rebuild.

| Installation action | When to use it |
| --- | --- |
| **Verify** | Diagnose installed-content integrity. Verification does not silently repair content. |
| **Repair** | Restore a managed Editor's exact signed package set. It is not a general upgrade or external-build repair. |
| **Manage Components** | Add/remove compatible Build Support for that specific Editor. Close the Editor first. |
| **Uninstall...** | Remove a healthy, inactive managed installation using its ownership records. |
| **Remove from Hub...** | Remove an external or missing-root registration where offered; this is not filesystem cleanup. |

If damaged managed content prevents uninstall, follow the requested repair-first workflow. Do not bypass ownership
checks by deleting registry/state records. If an exact repair package is unavailable, retain diagnostics for recovery.

## Sign In Through The Browser

1. Open Hub's account surface and choose **Continue in browser**.
2. Complete the Kéire website's account or GitHub sign-in.
3. Approve the browser's **Open Kéire Hub** request.
4. Return to Hub and confirm its account state changed. A website success page alone does not prove desktop completion.

The current account deployment uses the website/MySQL-backed service. The legacy filename `Config/Supabase.json` is
not evidence that the Hub should send you to a Supabase authorization host. If it does, record the URL and Hub build
identity: an outdated build or account configuration needs correction. A backend quota/restriction page cannot be
fixed by repeatedly retrying the desktop callback.

If nothing returns to Hub, check whether the browser blocked the external-app prompt and whether another Hub build owns
the protocol association. Use **Cancel browser sign-in** before starting a new attempt. Never share callback query
strings, authorization codes, tokens, or passwords with a bug report. Secure-storage failure may leave a session
memory-only; preserve the warning rather than assuming it will survive restart.

## Recover After An Offline Test

1. In Settings, turn **Offline mode** off and choose **Save settings**. An unsaved toggle is not applied configuration.
2. Restore network access and inspect the footer. **Connection issue** exposes details on hover; **Reconnecting** means
   recovery is still pending, not that the catalog is fresh.
3. Retry the affected catalog or account operation and confirm it finishes. A previously cached release list alone is
   not proof that the service recovered.
4. If the error persists, retain its text, time, affected operation, Hub version, and diagnostic bundle for the operator.

Do not disable TLS or signature verification, erase catalogs indiscriminately, or restart shared servers as a routine
client recovery step. A distribution outage and an account outage can have different causes. Offline local editing can
continue with installed healthy software; uncached downloads and online authorization cannot.

## Tasks, Notifications, And Build Support

In **Tasks and downloads**, use **Dismiss** on an ended task or **Clear finished** for completed entries. Closing the
panel only hides it; it does not clear history or cancel active work. Keep failure details before dismissing them.

From Editor Build Settings, **Open Build Support** requests the selected target for that Editor executable. Check the
Editor path/version in Hub, save your project, and close that Editor before component maintenance. Do not install into
another candidate merely because it has the same display version. Reopen the project and confirm Build Settings now
reports compatible support. See [Player Builds And Packages](PlayerBuildsAndPackages.md).

Continue with [Projects And The Editor](ProjectsAndEditor.md) or [Hub reference](../ProjectHub.md).
