# Build, select, verify

## Requirements

- A resident-authorized ECY-STAT thermostat and access to its legitimate pairing PIN.
- M5Stack ATOM Lite / ESP32 with 4 MB flash, USB cable and a computer.
- ESP-IDF **v6.0.2**, installed and activated using its official installation instructions.
- For the scheduler: an authorized Wi-Fi network and reachable time server at startup.

The original ATOM Lite is the hardware baseline. Other ESP32 boards need BLE,
sufficient memory and compatible ESP-IDF/NimBLE configuration; they are not
claimed tested. Raspberry Pi/Linux transport is not provided.

## 1. Build the commissioning example

From the repository root, with the ESP-IDF environment activated:

```sh
cd examples/commission
idf.py set-target esp32
idf.py build
idf.py -p YOUR_SERIAL_PORT flash monitor
```

Flashing and opening the monitor can reset the board. This example initializes
NVS without erasing it, starts Bluetooth, and lists up to 16 nearby ECY-STAT
candidates. It does not connect until you enter a selection. Addresses in the
terminal are private; do not publish them.

Select your thermostat by its number. Do not treat the strongest signal as
proof of identity. Obtain the PIN through the resident app's supported process
(the tested setup used myPERSONIFY's **Show code** action). Enter exactly six
digits and press Enter when the terminal requests the PIN. The firmware does
not log the PIN. Only attempt pairing with your authorized device.

The example reads Target and Fan, checks authenticated/bonded connection state,
and saves the peer identity only after successful decoding. Check the displayed
values against your thermostat. The new selection/persistence flow still needs
hardware validation; the original experiment demonstrated the underlying pairing
and readback operations, not this exact packaged sequence.

If a peer is already selected, commissioning stops instead of replacing it.
If a bond is rejected, neither example silently deletes it. Stop and diagnose
before deliberately changing an existing pairing. No automatic recovery/erase
command is provided in this preview. A failed save may leave a bond without a
selected identity; reboot and retry the same authorized device first.

## 2. Configure your schedule

Copy `examples/scheduler/config.example.hpp` to a private location outside the
repository. Edit network credentials, POSIX timezone, time server and the sorted
list of events. The example timezone is Eastern US; it is not auto-detected.

Only `FanMode::On` and `FanMode::Auto` are supported. Use Fahrenheit values
appropriate for your thermostat. The original schedule validated 66°F and 68°F;
other values and thermostat limits require your own verification.

## 3. Build and flash the scheduler

Exit the commissioning monitor (`Ctrl+]`), then from the repository root:

```sh
cd examples/scheduler
idf.py set-target esp32
idf.py -D FROST_RUNTIME_CONFIG_HEADER=/absolute/path/to/your/frost-config.hpp build
idf.py -p YOUR_SERIAL_PORT flash monitor
```

Both examples use the same standard single-app partition layout. Normal flashing
preserves NVS, including the saved selection and BLE bonds; do not erase flash
between the examples. Check your existing partition layout before reusing a
board from a different application. A build without a configuration header stops
before starting radios or scheduling.

Wait for synchronized time and a verified Target/Fan result. An ATT write
acknowledgment alone is not success. Compare the actual panel values. The
scheduler uses only the saved identity and refuses fresh pairing.

After the first synchronization in a boot, temporary Wi-Fi loss does not stop
scheduling; recovery requests a time refresh. A new boot always requires initial
time synchronization. Offline clock drift and real outage recovery remain
hardware acceptance items. There is no maximum offline age.

## Privacy and troubleshooting

- Keep the private header, configured `.bin`/`.elf` files, serial captures and
  NVS dumps out of public repositories. Build outputs can contain credentials.
- An unrecognized saved identity or rejected bond requires explicit diagnosis;
  the controller does not select a different nearby thermostat.
- If a startup result differs from the expected schedule, capture boot/time and
  verification outcomes before reflashing. A cold-start mismatch issue in the
  original deployment remains unresolved.
- This preview has no web/mobile UI, remote telemetry or persistent event log.
