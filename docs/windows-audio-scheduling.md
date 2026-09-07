# Windows transmit scheduling

The Windows client audio callback publishes frames to a bounded queue and signals
a native auto-reset event. The transmit worker drains that queue before waiting
again. Signals are retained across the empty-queue/wait transition; shutdown also
signals the event. The callback never waits for the sender or takes a user-space
mutex to signal it. Network I/O and pitch/meter processing remain on the worker.

The worker registers with Windows MMCSS as `Pro Audio` for its lifetime. If that
registration is unavailable, transmission continues at the existing high thread
priority, and diagnostics report MMCSS as inactive. This does not change the
process priority class. The shared engine gives the Windows plug-in the same
transmit scheduling; other platforms retain their existing polling behavior.

## Reproducing the foreground/background issue

1. Start a fresh standalone client and join a two-person session.
2. Record the ASIO device, sample rate, driver buffer size, Windows version, and
   server buffer settings.
3. Alternate between JammerNetz and the effects DAW in the foreground. Also try
   leaving JammerNetz partly visible while the DAW has focus.
4. Keep the client log and note when the sound changes. Repeat with a fresh client
   and the previous server buffer settings if needed.

While callbacks are running, the standalone logs an `Audio timing` summary about
every five seconds, from the message thread. Values are **lifetime maxima**, in
milliseconds, so restart the client for independent comparisons. A blocked message
thread can delay the log without preventing the audio counters from updating.

| Field | Meaning |
| --- | --- |
| callback gap | Largest interval between callback starts |
| gap excess | Largest gap minus the preceding callback's sample duration, clamped to zero |
| processing | Largest elapsed time inside audio processing |
| transmit queue | Largest frame enqueue-to-worker-processing delay |
| queue-to-send | Largest enqueue-to-send-attempt delay, including pitch and meter preparation |
| transmit MMCSS | Whether registration is currently active |
| transmit drops | Cumulative dropped transmit frames |

The first callback after device prepare/release does not contribute a gap, so
device downtime is excluded. Gap excess describes observed callback spacing, not
a driver-reported xrun. Queue-to-send excludes the socket call and network transit;
it starts at frame enqueue, not at physical capture or the start of an ASIO block.
The maxima can come from different frames and should not be subtracted to infer
an exact processing cost.

If callback gaps grow, investigate callback/driver scheduling. If transmit queue
delay grows while callback spacing remains regular, investigate worker scheduling.
If queue-to-send grows with short queue waits, investigate packet preparation.
Correlate these with server arrival gaps to distinguish local delays from network
jitter. This instrumentation does not establish that every foreground glitch has
the same cause.
