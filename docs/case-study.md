# From a thermostat to a working automation MVP

## Problem

I wanted a resident-controlled ECY-STAT to switch temperature and fan selection
on a daily schedule, using a small controller that could stay powered without
my computer. The starting point was a thermostat and its authorized resident
app, not a ready-made integration for my use case.

## My role

I defined the desired behavior, performed the physical experiments, checked the
thermostat's response, challenged assumptions when observations disagreed, and
directed the implementation toward a working MVP. This repository presents the
reusable result and its evidence limits.

## Approach

I studied authorized BLE activity and changed one variable at a time to identify
the commands required for Target, FanOn and FanAuto. The implementation separates
command encoding, Bluetooth transport, verification and scheduling so that most
behavior can be checked without a thermostat.

One central decision was to distinguish transport acknowledgment from successful
application. A thermostat accepting a BLE write does not prove it has reached
the requested state. Frost reads the settings back, waits for fan convergence,
and checks Target again because the two changes are not atomic.

The scheduler selects the state appropriate for the current time instead of
queuing missed commands. Successful periods do not repeat writes. Boot waits
for network-synchronized time; manual changes remain until a new event or reboot.

## Result

The original ATOM Lite installation completed real morning and night transitions
with readback verification. Subsequent user observations matched the intended
schedule. The work spanned at least five calendar weeks of research and delivery;
that is not a claim about billable hours or continuous development time.

The public library adds a configurable daily example and explicit device
selection. Those packaging changes are separately build/host-tested; their new
hardware path is not yet accepted. The original cold-start mismatch report also
remains open. See [validation](validation.md) for the exact distinction.

## What this demonstrates

- Turning an ambiguous hardware integration into a small, testable protocol surface.
- Designing around observed behavior and correcting unsupported assumptions.
- Reporting partial failure instead of claiming success from an acknowledgment.
- Delivering a working MVP, then extracting reusable code with explicit limits.

No claimed energy savings, universal compatibility, or invented reliability
statistics. The demonstration is the actual schedule behavior and its verification.
