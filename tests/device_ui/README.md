# OpenAce device UI tests

These tests drive the web interface of a physical OpenAce device and verify every
write through its HTTP API.

## Install

```bash
cd tests/device_ui
python3 -m venv .venv
.venv/bin/pip install -e .
.venv/bin/playwright install chromium
```

For the optional Bluetooth COBS test, also install `.venv/bin/pip install -e '.[ble]'`.

## Run

Reversible UI and API tests:

```bash
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest
```

Run one area:

```bash
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest tests/test_gatas_connect.py
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest tests/test_foreflight_discovery.py
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest tests/test_flash_persistence.py
```

Inspect the device's outgoing Bluetooth COBS stream (use the device's Bluetooth
address, or its CoreBluetooth UUID on macOS):

```bash
.venv/bin/pytest -s --cobs-ble-address DEVICE_ADDRESS tests/test_cobs_ble.py
```

The test listens for 15 seconds on characteristic FFE2, checks complete COBS
frames and rejects consecutive zero delimiters, and prints counts by decoded
message type. It requires a configuration broadcast (type 5), so GatasConnect
must have an active output and Bluetooth must be enabled. It observes outbound
BLE only; absent types may simply not have been emitted during the observation.
It does not send commands, validate every message's contents, or inspect UDP.
UDP currently uses zero padding for an Android hotspot workaround, so the BLE
single-delimiter assertion must not be applied directly to UDP datagrams.

To inspect GatasConnect UDP instead:

```bash
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest -s -rs tests/test_cobs_udp.py
```

The test needs GatasConnect and GatasConnectUDP enabled, and binds UDP port 3000
on the local interface used to reach the device. It temporarily sets the
GatasConnectUDP server IP to that interface, then restores the original setting
without saving to flash. Configuration writes may mark the device dirty. The
test checks each datagram's COBS frame and permits its trailing zero padding;
it cannot distinguish extra delimiters from this padding. A skipped test prints
which modules must be enabled when run with `-rs`.

The protocol type byte is the first byte after COBS decoding. The following is
the protocol inventory, not a promise that every type appears on the BLE stream:

| Type | Message | Role in OpenAce |
| --- | --- | --- |
| 1 | Aircraft position V1 | Incoming traffic; deprecated |
| 2 | Ownship position request V1 | Legacy outgoing position request |
| 3 | Aircraft configuration V1 | Deprecated, no longer defined |
| 4 | Select aircraft | Incoming selection command |
| 5 | Aircraft configuration V2 | Outgoing configuration broadcast |
| 6 | GDL90 | Outgoing Bluetooth bridge payload |
| 7 | Set WiFi mode | Incoming command |
| 8 | Aircraft position V2 | Incoming traffic |
| 9 | Aircraft position V3 | Incoming traffic with pressure altitude and QNH |
| 10 | Ownship position request V2 | Outgoing position request |
| 11 | Ownship pressure V1 | Incoming pressure and QNH |
| 12 | Device action V1 | Incoming Bluetooth action with boot ID |

Traffic requests depend on GPS fix and configuration; ground stations also
require recent radio traffic. These conditions do not guarantee nearby traffic.

Validate direct radio traffic with a second transmitting GATAS:

```bash
OPENACE_DEVICE_URL=http://192.168.178.227 .venv/bin/pytest \
  --run-radio-traffic tests/test_aircraft_tracker_radio.py
```

Use `--headed` to watch the browser. Playwright traces and screenshots are
retained for failures under `test-results/`.

The suite creates unique temporary aircraft and removes them afterward.
The ForeFlight discovery test listens on an unused local UDP port, verifies the
device does not send to it before discovery, advertises it to the device, and
then requires several GDL90 packets including a heartbeat.
It skips if this computer is already registered or all device port slots are
occupied, because the device cannot register a new port in those conditions.
The flash persistence test temporarily changes the active aircraft category,
calls `SaveBr`, and uses the `GATAS_DEBUG`-only `CompareBr` endpoint to verify
the flash contents immediately. It restores and saves the original aircraft
record before completing; no device restart is required.
