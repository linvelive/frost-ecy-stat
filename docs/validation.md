# Validation and limits

Evidence from the original private deployment must not be confused with checks
of this newly extracted public library.

| Claim | Evidence |
| --- | --- |
| Target 66°F/68°F and FanOn/FanAuto commands | Original authorized captures, manual reproduction, ATOM Lite write/readback trials |
| Real morning/night schedule transitions | Original controller readbacks and independent user observations |
| Matching-state recovery after power cycle | Original one-cycle readback; no new writes required |
| Configurable daily list, date-aware events, retry/manual-change policy | Public host tests; not flashed |
| New explicit selection, saved identity and direct reconnection | ESP-IDF build check only; hardware acceptance pending |
| Initial SNTP gate and offline scheduling policy | Host tests and build checks; public hardware acceptance pending |
| Apply current state after cold boot when settings differ | Intended/source-tested behavior; a contrary report on the original installed image remains unresolved |
| Other ESP32 boards, other ECY-STAT installations, Linux | Not verified; Linux adapter not implemented |

## Operational contract

The controller reads before applying. A matching Target/Fan state produces no
writes. Failed reads do not justify a blind write. On mismatch, it writes Target,
verifies Target, writes Fan, polls Fan, and rereads Target. There is no rollback:
a fan failure can leave a successfully changed target in place. Inspect field
results rather than reducing every outcome to a boolean.

After a successful schedule event, the example does not continuously enforce
its settings. Manual changes remain until another event or restart. Failed
applications retry, so a manual change during a pending failure is not treated
as a lasting override. On startup or after skipped periods, only the current
state is selected. A one-event schedule is distinguished by local calendar day.

Clock corrections and DST use current local wall time. Skipped events are not
replayed. A backward clock change can select an earlier period again; this is
state reconciliation, not a guarantee of exactly-once event delivery across
clock changes. Hardware DST transitions and long-term drift are untested.

Both BLE operations and user PIN input have finite waits. Setup may need a retry
if the PIN is entered too late. Advertising addresses may change; saving the
paired identity is intended to support reconnection, but resolving-list behavior
on this thermostat still needs real verification. Single controller/transport,
single calling task; no concurrent BLE clients are coordinated by this library.

## Acceptance still needed before a stable release

1. Run the new commissioning example, selecting the resident-authorized device.
2. Reboot without the setup terminal and confirm saved-peer authenticated access.
3. Verify writes/readbacks from a mismatching state after cold boot.
4. Observe configurable schedule transitions and a manual override.
5. Exercise network loss/recovery after initial synchronization.

These are future acceptance checks, not completed results or an instruction to
run all steps without observing intermediate outcomes. A source preview can be
reviewed now; it should not be represented as a universally tested installation.
