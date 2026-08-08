# KEIRE-REPLAY-0002: Replay session failure

Kéire stopped a replay session because recording, file decoding, checkpoint restoration, state capture, or atomic
recording publication failed. The diagnostic message identifies the failed operation and preserves the original error.

Verify the replay path is writable, the file is within the configured encoded and decoded size budgets, every
serializer is registered at the recorded version, and the recording was produced by a compatible certified build.
Corrupt or untrusted replay files should be discarded rather than retried.
