# Device actions and state notifications

## Action wire format

Device actions use `DEVICE_ACTION_REQUEST_V1` (type 12) over Bluetooth.
The decoded request is exactly three bytes:

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 1 | Type 12 |
| 1 | 1 | Action: Save=1, Restart=2, USB boot=3 |
| 2 | 1 | Current 8-bit boot ID from the configuration broadcast |

Requests use COBS framing with a terminating zero. For example, Save with boot
ID `0xab` is decoded as `0c 01 ab` and framed as `04 0c 01 ab 00`.

The parser rejects malformed requests, unsupported actions, UDP requests and
requests for a different boot ID. Device actions bypass the legacy startup
holdback. Status (action 0) and the old 16-byte Prepare/Execute format are not
accepted. There are no request IDs, sessions, Prepare/Execute phases or type-13
replies. Clients observe state in periodic type-5 configuration broadcasts.

## Deferred execution

The GatasConnect COBS parser publishes `DeviceActionRequestMsg`. Config receives
it and stores the action in `etl::optional<GATAS::DeviceAction>`; it performs no
flash write or reset in that request handler. A newer request replaces the
pending action.

Config also subscribes to `IdleMsg` from Idle. On an idle tick it reads the
pending action and dispatches it:

| Action | Execution from the idle tick |
| --- | --- |
| Save | Attempts `configMutex` without waiting, then calls `saveToFlash()` if acquired. |
| Restart | Publishes `SystemActionMsg` to Idle, which calls the watchdog reset. |
| USB boot | Publishes `SystemActionMsg` to Idle, which calls the ROM USB reset. |

After dispatch, Config clears the pending action if it still matches the action
it executed. A different action queued during execution remains pending. If Save
cannot acquire the configuration mutex, it remains pending for a later tick.

`saveToFlash()` checks store capacities, serializes a clean image into the volatile
store, writes it to persistent flash and verifies the complete readback. An
unchanged image avoids another flash write. A failed write restores the dirty
flag in retained RAM. It then publishes
`ConfigUpdatedMsg{*this, Configuration::NAME}`. A busy configuration mutex skips
the save for that tick, leaving the action pending for a later tick.

USB boot does not implicitly save changes. Restart uses the existing retained
RAM configuration, including the latest BLE aircraft selection. Flash-write
failures leave the configuration dirty so periodic broadcasts show it as unsaved.

The web SaveBr endpoint calls the same save helper synchronously under its
configuration mutex and reports failure if persistence could not be verified.
Web Restart and UsbBoot publish `SystemActionMsg` directly;
they do not use the pending device-action slot.

## Configuration broadcasts

`sendAircraftConfiguration()` snapshots the cached configuration values and
dirty flag, allocates and encodes one type-5 COBS frame, then sends one
`GatasConnectTx` through the configuration outputs, including Bluetooth.
Broadcasts run once per second without session targeting or a separate state
response frame.

Relevant decoded fields are:

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 1 | `AIRCRAFT_CONFIGURATIONS_V2` = 5 |
| 1 | 1 | FLAGS: bits 0–1 encode WiFi mode (NC/AP/CLIENT); bit 2 (`0x04`) is dirty; bits 3–7 are reserved. |
| 13 | 3 | Major, minor and bugfix version bytes; currently hardcoded to `3, 6, 0` in the serializer. |
| 16 | 1 | 8-bit boot ID owned by GatasConnect. |

The serializer currently does not use the build-derived version constants for
these three bytes. GatasConnect increments its boot ID in `postConstruct()`;
the ID is stored in uninitialized RAM, can wrap through zero and can repeat.
It is not a globally unique identifier or an authentication token.

GatasConnect copies dirty state through `Configuration::isDirty()` during
construction and synchronously from `ConfigUpdatedMsg` notifications. No
Configuration reference is retained or read from the timer callback.

Bluetooth assembles complete frames per connection. The shared COBS parser
discards buffered fragments when the transport changes; there is no BLE session
metadata in the bus messages.

See [message-bus.md](message-bus.md) for the message routes.
