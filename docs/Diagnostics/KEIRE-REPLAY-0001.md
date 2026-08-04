# KEIRE-REPLAY-0001: Replay divergence

The canonical fixed-tick state hash differs from the hash stored in a replay. Verification stops at the first
divergent tick and records both digests.

Confirm that the engine build, ordered source-module catalog, project descriptor, cooked content, input-map
fingerprint, and deterministic configuration match the recording. Simulation-affecting modules must register complete
deterministic serializers. Use the timeline to restore the previous checkpoint and step forward while inspecting the
first state difference. Performance captures report divergence but do not promise identical intermediate results.
