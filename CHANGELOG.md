# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

[Unreleased]

### Added

-

### Changed

-

### Deprecated

-

### Removed

-

### Fixed

-

### Security

-

## [v3.6.0-rc1] - 2026-10-25

### Added

- Added periodic device-state broadcasts with supported capabilities, boot IDs,
  and unsaved-configuration status, without request IDs, results, or sessions.
- Response to specific DeviceActions so the device can be restarted/data saved over BLE via cobs

## [v3.5.1] - 2026-10-15

### Added

-

### Changed

- Changed pBuffers size to 20
- Changed PoolAllocator to 610

### Deprecated

-

### Removed

-

### Fixed

-

### Security

-

## [v3.5.0] - 2026-10-14

### Added

- Added regression coverage for aircraft-tracker expiry, prediction, capacity cleanup,
  radio-source priority, nearest-aircraft selection, and GDL90 encoding/decoding.
- Added GDL90 packing-failure statistics to the service diagnostics.
- Added version-negotiated aircraft position V3 messages with optional barometric altitude.

### Changed

- Aircraft tracking now filters expired measurements before scheduled output and ADS-L
  uplink selection, while leaving storage cleanup to the existing maintenance paths.
- Full tracker storage is reclaimed by removing the farthest aircraft one at a time,
  preserving clustered traffic and recalculating the admission radius during cleanup.
- Accepted aircraft updates no longer reset their existing output deadline, preventing
  high-rate input from starving other aircraft.
- Fresh radio positions retain priority over ADS-B and MLAT updates until the radio
  priority timeout expires.
- GDL90 ownship and traffic altitude handling now follows the configured altitude datum,
  target traffic prefers received barometric altitude with an MSL fallback, and ForeFlight
  identification advertises the matching datum.

### Deprecated

-

### Removed

-

### Fixed

- Fixed tracker output and uplink selection so stale aircraft cannot consume output slots
  or displace valid nearer traffic.
- Fixed GDL90 heartbeat, initialization, uplink, and related field byte ordering to match
  the protocol, and corrected escaping, framing, CRC, capacity, and message validation.
- Fixed GDL90 traffic address type and NACp encoding used by the service.
- Fixed StaticGPS NTP synchronization after the 32-bit monotonic timer wraps by retaining
  the full-width receive timestamp.
- Fixed GPS RMC/local-time diagnostics so millisecond-level second-boundary and normal
  sentence transport delays do not produce false warnings.
- Imporved sx1262 robustness while changing radio modulations and syncwords

### Security

-

## [v3.4.0] - 2026-09-06

### Added

- [Experimental] Added a configurable StaticGPS module for stationary installations that generates NMEA position data through the normal GNSS pipeline and synchronizes its clock using NTP.

### Changed

-

### Deprecated

-

### Removed

-

### Fixed

- Fixed SX1262 DIO1 interrupt transitions by disabling the GPIO handler before clearing stale radio IRQs and remapping RX_DONE or TX_DONE.
- Replaced the SX1262 maximum-duration hardware TX timeout with bounded 10 ms GFSK and 55 ms LoRa timeouts so failed transmissions return to the configured fallback state promptly.

### Security


## [v3.3.0] - 2026-09-01

### Added

- Added once-per-second DataPort PFLAU heartbeats with tracked-aircraft RX counts and GPS-derived TX/GPS status.
- Bluetooth also sends on Nordic GATT
- RB Avionics (https://www.rbavionics.com) compatibility thanks to Duke306 (https://github.com/duke306)

### Changed

-

### Deprecated

-

### Removed

-

### Fixed

- Restored u-blox M8 navigation configuration using the Airborne `<4g` dynamic platform model.

### Security

-

## [v3.2.2] - 2026-08-08

### Added

-

### Changed

- Clarified in the GDL over UDP module overview and configuration panel that ForeFlight clients and their advertised GDL90 ports are discovered automatically.
- Extended the ForeFlight simulator to listen on its advertised GDL90 UDP port and count received packets.
- Added a device integration test that verifies GDL90 UDP traffic starts only after ForeFlight advertises its listening port and includes heartbeat messages.

### Deprecated

-

### Removed

-

### Fixed

-

### Security

-

## [v3.2.1] - 2026-08-06

### Added

-

### Changed

-

### Deprecated

-

### Removed

-

### Fixed

- Fixed the aircraft selector showing the last configured aircraft after a webpage reload instead of the active aircraft saved in the device configuration.

### Security


## [v3.2.0] - 2026-07-26

### Added

- Added MLAT as a recognized aircraft data source with full and compact display names.
- Added a combined UDP and Bluetooth output mode for GATAS Connect.

### Changed

- Redesigned the SystemGUI with Pico CSS, responsive navigation, consistent forms and dialogs, clearer device status, and improved module monitoring layouts.
- SystemGUI navigation now follows URL hashes so pages can be linked directly and restored after reload.
- SystemGUI now enables saving modified RAM configuration to flash only when no aircraft or module configuration editor is open.
- Combined GATAS Connect output now prefers UDP traffic while UDP responses are active and automatically restores Bluetooth requests when UDP becomes silent.
- Improved RP2040 startup memory allocation to reduce module load failures.

### Deprecated

-

### Removed

-

### Fixed

- Fixed RP2350 Bluetooth flash storage overlapping saved configuration, so settings survive restarts and firmware updates.
- Fixed configuration updates so invalid or incomplete requests are rejected without partially changing settings, including creation of new aircraft entries.
- Fixed fragmented and larger web configuration requests being truncated or partially applied.
- Fixed GATAS Connect decoding of larger aircraft frames and variable-length callsigns, preventing corrupted traffic and incorrect data-source values.
- Fixed radio transmit scheduling when all configured protocol timings are active.
- Improved radio and aircraft-tracking queue handling for reliable processing across tasks.
- Fixed SX1262 receive metadata being associated with the wrong tuned protocol and corrected transmit-power limiting.
- Initialized GPS satellite-in-view counters, radio identifiers, and AirConnect client state before use.
- Aircraft tracking now preserves fresh positions received directly over radio when matching ADS-B or MLAT updates arrive, until the radio-priority timeout expires.

### Security

-

## [v3.1.0] - 2026-07-13

### Added

- Experimental aircraft path prediction in `AircraftTracker`, using recent position, speed, heading, vertical speed, and turn-rate history to extrapolate short gaps in tracked traffic.
- Desktop test coverage for aircraft path prediction and updated antenna-radiation bearing behavior.
- Squawk code tracking on `AircraftPositionInfo`, including ADS-B squawk capture and an `AircraftTracker` option to show known squawk codes instead of callsigns.

### Changed

- Added an `AircraftTracker` configuration and web UI option to enable or disable path prediction.
- `AircraftTracker` now keeps lightweight predictor state for nearby traffic and can forward extrapolated positions during scheduled client updates.
- Normalized several tracker/core numeric types and math helpers, including 32-bit ellipsoid heights and float-specific angle/CPR calculations.
- `AIRCRAFT_POSITION_TYPE_V2` binary messages now include a signed 16-bit squawk field after aircraft category; `-1` means unknown.

### Deprecated

-

### Removed

-

### Fixed

- Fixed antenna radiation pattern bearing calculation so radial placement is based on ownship track and target position, not the target aircraft track.
- Fixed protocol/config bounds handling for tracked-aircraft distance and transmitted altitude values in FLARM, FANET, and OGN paths.
- OGN, FLARM, and binary V2 aircraft positions with unavailable or out-of-window minute-relative timestamps are now logged and discarded instead of being treated as current.
- `AircraftTracker` now rejects out-of-order position updates before they can replace newer coordinates or desynchronize predictor history.
- Restored timer-driven `AircraftTracker` scheduling so continuous position notifications cannot suppress heartbeat and predicted-position output.
- Added timer-driven SX1262 TX timeout recovery so a missing `DIO1_TX_DONE` interrupt cannot leave the radio stuck waiting for another task notification.
- Aircraft already tracked are now removed immediately when a current position places them outside the adaptive tracking radius.
- Aircraft path prediction now prefers protocol-provided horizontal turn rates, with heading-history derivation retained as a fallback.

### Security

-

## [v3.0.1] - 2026-06-13

### Added

- Added support for two named SPI buses devices can communicate with

### Changed

-

### Deprecated

-

### Removed

-

### Fixed

- New way of setting processor speed did not always work, back to 200Mhz

### Security

-

## [v3.0.0] - 2026-06-11

### Added

- Initial GATAS Companion support with a new `GatasConnect` core module and a separate `GatasConnectUDP` transport module.
- Bluetooth transport for GATAS Connect, including dedicated NMEA and binary GATT characteristics for companion-device integration.
- Optional GDL90-over-Bluetooth bridge using COBS-framed payloads for companion applications.
- Debug-only FreeRTOS queue registry entries for mutexes, to improve visibility in `GATAS_DEBUG` builds.
- Frontend unit-test support for `SystemGUI` via `npm test`.
- Option to prefix aircraft callsigns with a two-letter datasource code.
- Remote switching between Wi-Fi access-point and client modes from companion/mobile applications.

### Changed

- Split GATAS Connect so COBS framing and request generation live in `GatasConnect`, while UDP transport is handled by `GatasConnectUDP`.
- Extended the web UI to configure GATAS Connect output, pin code, and GDL90 bridge settings, plus a separate UDP server configuration module.
- Improved module monitoring to render structured object and array values more clearly.
- Updated the bundled device database.
- Reworked `vDiagnosticsTask()` so task runtime statistics are reported from `uxTaskGetSystemState()` directly, with clearer boot-time and recent-window CPU metrics for SMP builds.
- Refactored `CoreUtils` from a static utility class into a namespace, while keeping mutable internal state private to the implementation file.
- Bluetooth advertising now splits the local name between the primary advertisement and scan response payloads, improving visibility of longer device names while keeping the custom service UUID advertised.
- Refined `GatasConnect` web configuration so the GDL90 bridge option is only shown for Bluetooth output modes, and documented frontend test usage.
- Optimised SX1262 protocol reconfiguration by tracking the currently programmed protocol and modulation, avoiding unnecessary full radio reconfiguration while keeping explicit standby mode selection.
- Improved aircraft tracker performance by about 20%.
- Renamed `OGN1` to `OGN` in UI and configuration-facing labels.

### Deprecated

-

### Removed

-

### Fixed

- Fixed misleading task runtime reporting in diagnostics, where formatted runtime text could be mismatched against sorted task names.
- Fixed diagnostics labels and notes so CPU usage on the dual-core RP2040 SMP build is presented more honestly.
- Fixed aircraft tracker antenna polar output so it only reports radio-backed data sources, avoiding invalid transport-category entries in the UI.
- Fixed SX1262 LoRa RX bandwidth mapping for 500 kHz channels and capped TX power correctly at the radio maximum.
- Fixed radio receive statistics for FLARM, OGN so polar/range tracking only counts valid in-range packets.
- Fixed Bluetooth NMEA notifications so buffered data is only discarded after a successful notify call.

### Security

-

## [v2.1.1] - 2026-04-30

### Added

-

### Changed

-

### Deprecated

-

### Removed

-

### Fixed

- Better rendering of RadioTUnerTX and Protocol Timing in the UI
- Moved the 'action' buttons to a seperate page in the UI

### Security

-

## [v2.1.0] - 2026-04-28

### Added

-

### Changed

- FLARM packets will now be corrected for bit flips when possible to improve reception on larger distances

### Deprecated

-

### Removed

-

### Fixed

- AntennaRadionPattern was alligned to north instead of track of aircraft. Only issue in the web interface

### Security

-

## [v2.0.1] - 2026-04-23

### Added

- Prefer radio-derived positions over ADS-B when an aircraft is received from multiple data sources. Only switch to ADS-B if the radio position is older than
  4,000,000 µs (4 seconds). This avoids sudden position jumps, as ADS-B data is typically delayed. And we trust radio positions more.

### Changed

-

### Deprecated

-

### Removed

-

### Fixed

-

### Security

-

## [v2.0.0] - 2026-04-13

### Added

- ADSL Traffic Uplink on O-Band for both reception and transmission (in ground station mode)
- Groundstation mode that can show a static object at any height.
- Send Traffic over ADSL using uplinkTraffic message
- Low power mode during development
- Allow for per protocol RX or TX, or RX and TX selection
- Show in aircraft tracker the aircraft that it is tracking
- Adaptive protocol prioritisation. This will allocate more listening slots for protocols that is actually received to increase pings per aircraft

### Changed

- When GATAS is Stationary, reduce TX times for positional data to average of 5..6 seconds
- Use etl::delegate instead of etl::function
- Stopped storing a 'GATAS::*Msg' in local objects, always copy the contents as a rule
- Update list of possible Aircraft types in UI
- Update DDB
- Better represent RX Schedule with multiple protocols
- Use xTaskNotifyWait instead of ulTaskNotifyTake
- Removed the mutexes from the MessageRouter

### Deprecated

-

### Removed

-

### Fixed

- Regression on FANET TX
- Possible zero or negative values in FreeRTOS delays
- Fixed Incorrectly handle of device error on transceiver

### Security

-

## [v1.2.0] - 2026-03-12

### Added

- Show polar diagrams one ranges per each protcol on AircraftTracker

### Changed

- Update on the web interface where dimensions are shown instead of raw numbers
- Update on the aircraft database

### Fixed

- GDL90 Bug where negative lat/log where not resolved correctly on output

## [v1.1.0] - 2026-03-02

- Initial release of GATAS
