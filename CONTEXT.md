# Frost ECY-STAT context

The domain terms used by the library. Behavioral contracts and their evidence
live in [docs/validation.md](docs/validation.md).

## Thermostat state

**Target:** The thermostat's requested temperature setpoint in Fahrenheit,
distinct from measured room temperature.

**Fan mode:** `FanMode::On` or `FanMode::Auto`. A fan selection is separate from
the thermostat's heating/cooling mode.

**Desired state:** A Target and Fan requested together, represented by
`DesiredState`. The two fields are applied separately, without atomicity or
rollback.

**Verified:** Application-level readback matches both requested fields. An
ATT/GATT write acknowledgment alone does not establish this result.

**Partial failure:** An application can change one field without successfully
verifying the whole desired state. `ApplyResult` carries each field's outcome.

## Device identity

**Commissioning:** Explicit selection of an authorized thermostat, PIN pairing
and authenticated readback before saving its identity.

**Selected peer:** The saved thermostat identity used for later connections.
It is distinct from a nearby advertising candidate or the strongest signal.

## Schedule and time

**Schedule entry:** A daily local hour/minute boundary and its desired state.

**Schedule event:** An entry on a particular local calendar day. The same entry
on the next day is a new event, including in a one-entry schedule.

**Synchronized time:** UTC trusted after an explicit network synchronization
event in the current boot. A nonzero system clock alone is not sufficient.
The configured timezone maps UTC to local schedule time.
