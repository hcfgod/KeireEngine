# Isolated Windows validator firewall rules

The default rule names remain `Keire Marketplace Validator - <program role>` with no group. Existing rules are reused only when their executable target and group match exactly. Configuration resolves all four executable paths and checks every existing rule before making changes. A conflicting or ambiguous rule fails without modifying any rule; it is never silently redirected to a new installation.

For a separate QA installation, choose a unique namespace and pass the same values to all three commands:

```powershell
-FirewallRulePrefix 'Keire QA 20261023' -FirewallRuleGroup 'Keire QA 20261023'
```

These optional arguments are supported by:

- `Deployment/configure-windows-validator-firewall.ps1`
- `Deployment/start-windows-marketplace-validator.ps1`
- `tests/KeireMarketplaceValidator.Tests/RunOfflineWorker.ps1`

Names allow ASCII letters, numbers, spaces, underscores, periods, and hyphens, start with a letter or number, and are limited to 80 characters. The group may be empty. Avoid reusing a QA namespace for another executable location. The acceptance runner forwards the namespace to both preflight and worker execution.

Generate a separate `-Attestation` file for that QA installation. Attestations record the namespace in addition to the exact executable paths and hashes; the launcher checks the supplied namespace. Existing default attestations remain supported. An attestation does not authorize changing existing firewall targets.

Configuration requires elevation. The focused regression test does not: run `pwsh -NoProfile -File tests/KeireMarketplaceValidator.Tests/TestFirewallIsolation.ps1`. All firewall commands are mocked. It checks isolated creation, same-target reuse, whole-plan collision rejection, group collision rejection, duplicate-rule rejection, and parsing/parameter validation of all three entrypoints. No real firewall rules are created or modified by that test.

After QA, remove only the exact rules belonging to that QA namespace after verifying their targets. Do not remove or reconfigure the installed production worker's rules. Firewall changes and service activation are separate operator actions.
