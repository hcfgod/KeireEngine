# KEIRE-AUDIO-0001: Audio streaming underrun

The real-time audio callback needed a decoded page that was not resident, so it emitted silence and advanced the
logical sample cursor. The callback never waits, allocates, decodes, or performs file I/O.

Check the Audio row in the Architecture dashboard. Repeated events usually mean the audio CPU budget or prefetch
window is too small, storage is unusually slow, or too many voices became active simultaneously. Increase the audio
streaming budget, reduce concurrent voices, or cook shorter encoded pages. A single event during a large load spike is
recoverable; sustained events should be treated as a content or budget problem.
