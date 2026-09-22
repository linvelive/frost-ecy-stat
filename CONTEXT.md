# Frost ECY-STAT context

The language of the public C++17 library and its ESP32 examples. Setup belongs
in [docs/setup.md](docs/setup.md); evidence and remaining acceptance belong in
[docs/validation.md](docs/validation.md).

## Thermostat state and verification

**Target:** The requested thermostat setpoint in Fahrenheit, distinct from
the measured room temperature. The library does not implement the HVAC loop.

**Fan mode:** The supported fan selections `FanMode::On` and `FanMode::Auto`.
These are not HVAC heating/cooling modes or calibrated fan-speed controls.

**DesiredState:** A target temperature and fan mode requested together.
Applying the two fields is not atomic and has no rollback.

**Codec:** The platform-independent functions that encode target/fan commands
and decode their readbacks. See
[ecy_stat_codec.hpp](components/frost/include/ecy_stat_codec.hpp).

**BleTransport:** The controller's interface for connection, discovery, writes
with response, reads, delays and disconnection. The ESP32 implementation lives
outside the portable component.

**EcyStatController:** The component that applies and verifies a desired state.
`ensure_applied` reads first, skips writes when both fields match, and refuses
to write when a complete valid readback cannot establish a mismatch. `apply`
performs the writes directly. See
[ecy_stat_controller.hpp](components/frost/include/ecy_stat_controller.hpp).

**Verified:** Application-level readback agrees with the requested Target and
Fan. An ATT/GATT write response alone is not verification. `ApplyResult` retains
field outcomes, including a partial failure after one field has changed.

## Identity and commissioning

**Commissioning:** Explicit selection of an authorized thermostat, legitimate
PIN pairing and authenticated readback before saving its peer identity.

**Selected peer:** The persisted thermostat identity used for later connections.
The scheduler does not select the strongest nearby signal, replace the selected
device or initiate fresh pairing. This packaged reconnection path still needs
the hardware acceptance documented in `docs/validation.md`.

## Scheduling and time

**ScheduleEntry:** A local hour/minute boundary and its desired state. A schedule
is a nonempty list with sorted, unique times; entries outlive the runner.

**ScheduleRunner:** Selects the currently applicable event, including on startup
or after skipped periods. Successful events suppress repeated applies; failures
retry. Manual changes after success persist until the next event or restart.
See [schedule.hpp](examples/scheduler/main/schedule.hpp).

**Synchronized time:** UTC trusted after an explicit network synchronization
event during the current boot. A nonzero system clock alone is insufficient.
The configured timezone converts UTC into local calendar time. Scheduling may
continue offline after initial synchronization; a new boot needs a new sync.
See [clock.hpp](examples/scheduler/main/clock.hpp).
