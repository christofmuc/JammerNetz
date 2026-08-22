# Client clock normalization

JammerNetz keeps one canonical wire format: 48,000 samples per second in
128-sample packets. Device and host clocks are converted at the client edge;
the server mixer therefore never has to combine different nominal sample
rates or reinterpret a packet's duration.

## Capture

Input is converted from the reported device/host rate to the canonical network
rate before packetization. The running callback-rate measurement may refine
that ratio when physical hardware drifts from the rate reported by its driver.
A 100 ppm dead band preserves the exact, filter-free path for well-clocked
48 kHz interfaces. This makes a 44.1 kHz participant and a measured 47,850 Hz
participant produce the same 375 network packets per second as a 48 kHz
participant instead of allowing either hardware clock to become room time.

## Playout

The received 48 kHz room stream is converted to the device/host rate. The
nominal ratio handles rates such as 44.1 kHz. A bounded queue-fill servo adds at
most 5,000 ppm correction when the physical output clock differs from the rate
reported by the driver. It has a two-frame control dead band and does not enter
the filter path for a stable nominal-48 kHz queue.

This deliberately favors bounded latency over perfect recovery from arbitrary
clock faults. Drift beyond the correction range, starvation, or severe burst
loss still triggers the existing fixed rebuffering policy; it does not grow an
adaptive jitter buffer.

## Resampler and licensing

The implementation pins untagged `minorninth/libresample` commit
`7cb7f9c3f72d4e6774d964dc324af827192df7c3`, whose upstream README identifies
the code as version 0.1.5. JammerNetz selects its permissive BSD license option.
Windows binary distributions install the upstream BSD notice under
`licenses/libresample`. The exact-unity path bypasses libresample and remains
sample-perfect.
