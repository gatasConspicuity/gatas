# OpenAce Message Bus

This document maps every message type to its sender(s) and receiver(s).

> **Maintenance note**: Keep this document split into separate flow diagrams. When updating it, change only the affected flow sections and do not collapse everything into one large combined flow unless explicitly asked.

> **How this was generated**: Senders were found by grepping `getBus().receive(` across all `.cpp` files. Receivers were found by grepping `void on_receive(const GATAS::` across all `.hpp` files. The class name was inferred from the enclosing file/class context.

> **Direct handoff note**: Some important current flows are shown even though they are not plain bus publishes, notably `Dump1090Client -> ADSBDecoder` and `Sx1262 -> RxDataFrameQueue`.

## Bus Semantics

- The firmware uses `GATAS::ThreadSafeBus<24>` from `src/lib/core/ace/messagerouter.hpp`.
- Dispatch is synchronous: publishers call `getBus().receive(...)` and the ETL bus delivers directly to subscribers.
- The bus itself does not queue or copy messages.
- `etl::shared_message` is technically supported but explicitly warned against.
- Some important data paths are not bus messages at all; those are called out below.

## Module Topology

```plantuml
' =====================================================================
' SYSTEM OVERVIEW
' =====================================================================
@startuml
title System Overview

left to right direction

rectangle "GPS Subsystem" {
    [AbstractGnss]
    [StaticGPS]
    [GpsDecoder]
}

rectangle "Radio Subsystem" {
    [Sx1262]
    [RxDataFrameQueue]
    [RadioTunerRx]
    [RadioTunerTx]
}

rectangle "ADSB Input" {
    [Dump1090Client]
}

rectangle "Protocols" {
    [Ogn1]
    [Flarm2024]
    [ADSLAce]
    [FanetAce]
    [ADSBDecoder]
}

rectangle "Tracking" {
    [AircraftTracker]
}

rectangle "Outputs" {
    [Gdl90Service]
    [GDLoverUDP]
    [DataPort]
    [GatasConnect]
    [GatasConnectUDP]
    [Bluetooth]
    [AirConnect]
}

rectangle "Infrastructure" {
    [Idle]
    [Config]
    [WifiService]
    [Bmp280]
    [PicoRtc]
}

[AbstractGnss] --> [GpsDecoder] : GPSSentenceMsg
[StaticGPS] --> [GpsDecoder] : GPSSentenceMsg
[Config] --> [StaticGPS] : ConfigUpdatedMsg
[StaticGPS] ..> [PicoRtc] : ppsEvent (NTP phase)\n(direct call)
[GpsDecoder] --> [Ogn1] : OwnshipPositionMsg
[GpsDecoder] --> [Flarm2024] : OwnshipPositionMsg
[GpsDecoder] --> [ADSLAce] : OwnshipPositionMsg
[GpsDecoder] --> [FanetAce] : OwnshipPositionMsg
[GpsDecoder] --> [ADSBDecoder] : OwnshipPositionMsg
[GpsDecoder] --> [AircraftTracker] : OwnshipPositionMsg
[GpsDecoder] --> [RadioTunerRx] : OwnshipPositionMsg
[GpsDecoder] --> [RadioTunerTx] : OwnshipPositionMsg
[GpsDecoder] --> [DataPort] : OwnshipPositionMsg\nGpsStatsMsg
[GpsDecoder] --> [GatasConnect] : OwnshipPositionMsg\nGpsStatsMsg
[GpsDecoder] --> [PicoRtc] : UtcTimeMsg
[Sx1262] --> [RxDataFrameQueue]
[RxDataFrameQueue] --> [Ogn1] : RadioRxManchesterMsg
[RxDataFrameQueue] --> [Flarm2024] : RadioRxManchesterMsg
[RxDataFrameQueue] --> [ADSLAce] : RadioRxManchesterMsg\nRadioRxMsg
[RxDataFrameQueue] --> [FanetAce] : RadioRxMsg
[Dump1090Client] ..> [ADSBDecoder] : ADS-B binary\n(direct receiveBinary)
[Ogn1] --> [AircraftTracker] : IngressAircraftPositionMsg
[Flarm2024] --> [AircraftTracker] : IngressAircraftPositionMsg
[ADSLAce] --> [AircraftTracker] : IngressAircraftPositionMsg\nIngressAircraftPositionsMsg
[FanetAce] --> [AircraftTracker] : IngressAircraftPositionMsg
[ADSBDecoder] --> [AircraftTracker] : IngressAircraftPositionMsg
[AircraftTracker] --> [Gdl90Service] : EgressAircraftPositionMsg
[AircraftTracker] --> [DataPort] : EgressAircraftPositionMsg\nTrackerStatsMsg
[AircraftTracker] --> [ADSLAce] : EgressAircraftPositionsMsg
[AircraftTracker] --> [ADSBDecoder] : AdapativeRadiusMsg
[RadioTunerRx] --> [Sx1262] : RadioControlMsg
[RadioTunerRx] --> [RadioTunerTx] : RadioControlMsg
[RadioTunerTx] --> [Ogn1] : RadioTxPositionRequestMsg
[RadioTunerTx] --> [Flarm2024] : RadioTxPositionRequestMsg
[RadioTunerTx] --> [ADSLAce] : RadioTxPositionRequestMsg
[RadioTunerTx] --> [FanetAce] : RadioTxPositionRequestMsg
[RadioTunerTx] --> [AircraftTracker] : RadioTxPositionRequestMsg
[Gdl90Service] --> [GDLoverUDP] : GdlMsg
[Gdl90Service] --> [GatasConnect] : GdlMsg
[GatasConnect] --> [GatasConnectUDP] : GatasConnectTx
[GatasConnect] --> [Bluetooth] : GatasConnectTx
[GatasConnectUDP] --> [GatasConnect] : GatasConnectRx
[DataPort] --> [Bluetooth] : DataPortMsg
[DataPort] --> [AirConnect] : DataPortMsg
[Idle] --> [DataPort] : Every1SecMsg
[Bmp280] --> [Ogn1] : BarometricPressureMsg
[GatasConnect] --> [Ogn1] : BarometricPressureMsg\nOwnship pressure altitude / QNH

@enduml
```

```plantuml
' =====================================================================
' GPS DATA FLOW
' =====================================================================
@startuml
title GPS Data Flow

left to right direction

package "GPS" {
    [AbstractGnss]
    [StaticGPS]
    [GpsDecoder]
    [PicoRtc]
}

package "Consumers" {
    package "Protocols" {
        [Ogn1]
        [Flarm2024]
        [ADSLAce]
        [FanetAce]
        [ADSBDecoder]
    }
    [RadioTunerRx]
    [RadioTunerTx]
    [Gdl90Service]
    [DataPort]
    [GatasConnect]
    [Bluetooth]
    [Sx1262]
    [Idle]
}

[AbstractGnss] --> [GpsDecoder] : GPSSentenceMsg
[StaticGPS] --> [GpsDecoder] : GPSSentenceMsg
[Config] --> [StaticGPS] : ConfigUpdatedMsg
[StaticGPS] ..> [PicoRtc] : ppsEvent (NTP phase)\n(direct call)
[GpsDecoder] --> [Ogn1] : OwnshipPositionMsg\nGpsStatsMsg
[GpsDecoder] --> [Flarm2024] : OwnshipPositionMsg
[GpsDecoder] --> [ADSLAce] : OwnshipPositionMsg\nGpsStatsMsg
[GpsDecoder] --> [FanetAce] : OwnshipPositionMsg
[GpsDecoder] --> [ADSBDecoder] : OwnshipPositionMsg
[GpsDecoder] --> [AircraftTracker] : OwnshipPositionMsg
[GpsDecoder] --> [RadioTunerRx] : OwnshipPositionMsg
[GpsDecoder] --> [RadioTunerTx] : OwnshipPositionMsg
[GpsDecoder] --> [Gdl90Service] : OwnshipPositionMsg\nGpsStatsMsg
[GpsDecoder] --> [DataPort] : OwnshipPositionMsg\nGpsStatsMsg
[GpsDecoder] --> [GatasConnect] : OwnshipPositionMsg\nGpsStatsMsg
[GpsDecoder] --> [Bluetooth] : OwnshipPositionMsg
[GpsDecoder] --> [Sx1262] : GpsStatsMsg
[GpsDecoder] --> [Idle] : GpsStatsMsg

[GpsDecoder] --> [PicoRtc] : UtcTimeMsg

[AbstractGnss] --> [DataPort] : GPSSentenceMsg
[StaticGPS] --> [DataPort] : GPSSentenceMsg

@enduml
```

```plantuml

' =====================================================================
' RADIO RX FLOW
' =====================================================================
@startuml
title Radio RX Flow

left to right direction

package "Radio" {
    [Sx1262]
    [RxDataFrameQueue]
    [RadioTunerRx]
}

package "ADSB Input" {
    [Dump1090Client]
}

package "Protocol Decoders" {
    [Ogn1]
    [Flarm2024]
    [ADSLAce]
    [FanetAce]
    [ADSBDecoder]
}

package "Tracking" {
    [AircraftTracker]
    [RadioTunerRx] as RTRx2
}

package "Monitoring" {
    [GatasConnect]
}

[RadioTunerRx] --> [Sx1262] : RadioControlMsg
[Sx1262] --> [RxDataFrameQueue]
[RxDataFrameQueue] --> [Ogn1] : RadioRxManchesterMsg
[RxDataFrameQueue] --> [Flarm2024] : RadioRxManchesterMsg
[RxDataFrameQueue] --> [ADSLAce] : RadioRxManchesterMsg\nRadioRxMsg
[RxDataFrameQueue] --> [FanetAce] : RadioRxMsg
[Dump1090Client] ..> [ADSBDecoder] : ADS-B binary\n(direct receiveBinary)

[Ogn1] --> [AircraftTracker] : IngressAircraftPositionMsg
[Flarm2024] --> [AircraftTracker] : IngressAircraftPositionMsg
[ADSLAce] --> [AircraftTracker] : IngressAircraftPositionMsg\nIngressAircraftPositionsMsg
[FanetAce] --> [AircraftTracker] : IngressAircraftPositionMsg
[ADSBDecoder] --> [AircraftTracker] : IngressAircraftPositionMsg
[GpsDecoder] --> [AircraftTracker] : OwnshipPositionMsg

note bottom of GatasConnect : GatasConnect also receives\nIngressAircraftPositionMsg\nfor radio-traffic awareness

note bottom of RTRx2 : RadioTunerRx also receives\nIngressAircraftPositionMsg\nfor traffic-biased scheduling

@enduml
```


```plantuml
' =====================================================================
' TRACKING AND OUTPUT
' =====================================================================
@startuml
title Aircraft Tracking and Output

left to right direction

[AircraftTracker]

package "Protocol Feedback" {
    [ADSLAce]
    [ADSBDecoder]
}

package "Output Services" {
    [Gdl90Service]
    [GDLoverUDP]
    [DataPort]
    [GatasConnect]
    [GatasConnectUDP]
    [Bluetooth]
    [AirConnect]
}

[AircraftTracker] --> [Gdl90Service] : EgressAircraftPositionMsg
[AircraftTracker] --> [DataPort] : EgressAircraftPositionMsg\nTrackerStatsMsg
[AircraftTracker] --> [ADSLAce] : EgressAircraftPositionsMsg
[AircraftTracker] --> [ADSBDecoder] : AdapativeRadiusMsg

[Gdl90Service] --> [GDLoverUDP] : GdlMsg
[Gdl90Service] --> [GatasConnect] : GdlMsg

[GatasConnect] --> [GatasConnectUDP] : GatasConnectTx
[GatasConnect] --> [Bluetooth] : GatasConnectTx
[GatasConnectUDP] --> [GatasConnect] : GatasConnectRx
[DataPort] --> [Bluetooth] : DataPortMsg
[Bluetooth] --> [GatasConnect] : GatasConnectRx
[Bluetooth] --> [AirConnect] : DataPortMsg
[DataPort] --> [AirConnect] : DataPortMsg

@enduml
```

```plantuml
' =====================================================================
' RADIO TX FLOW
' =====================================================================
@startuml
title Radio TX Flow

left to right direction

package "Radio Control" {
    [RadioTunerTx]
    [RadioTunerRx]
}

package "Protocols" {
    [Ogn1]
    [Flarm2024]
    [ADSLAce]
    [FanetAce]
}

package "Tracking" {
    [AircraftTracker]
}

package "Radio Hardware" {
    [Sx1262]
}

[RadioTunerRx] --> [Sx1262] : RadioControlMsg
[RadioTunerRx] --> [RadioTunerTx] : RadioControlMsg
[RadioTunerTx] --> [Ogn1] : RadioTxPositionRequestMsg
[RadioTunerTx] --> [Flarm2024] : RadioTxPositionRequestMsg
[RadioTunerTx] --> [ADSLAce] : RadioTxPositionRequestMsg
[RadioTunerTx] --> [FanetAce] : RadioTxPositionRequestMsg
[RadioTunerTx] --> [AircraftTracker] : RadioTxPositionRequestMsg

[Ogn1] --> [Sx1262] : RadioTxFrameMsg
[Flarm2024] --> [Sx1262] : RadioTxFrameMsg
[ADSLAce] --> [Sx1262] : RadioTxFrameMsg
[FanetAce] --> [Sx1262] : RadioTxFrameMsg

@enduml
```

```plantuml
' =====================================================================
' SENSOR FLOW
' =====================================================================
@startuml
title Sensor Data Flow

left to right direction

[Bmp280] --> [Ogn1] : BarometricPressureMsg
[GatasConnect] --> [Ogn1] : BarometricPressureMsg\nOwnship pressure altitude / QNH

@enduml
```

```plantuml
' =====================================================================
' TIMER / CONFIG / WIFI
' =====================================================================
@startuml
title Infrastructure Messages

left to right direction

package "Timers" {
    [Idle]
}

package "Configuration" {
    [Config]
}

package "Network" {
    [WifiService]
    [GatasConnect] as GC4
}

package "Timer Consumers" {
    [ADSBDecoder]
    [WifiService] as WS2
    [AirConnect]
    [AircraftTracker]
    [Bmp280]
    [DataPort]
}

package "Config Consumers" {
    [Sx1262]
    [GDLoverUDP]
    [Ogn1]
    [ADSLAce]
    [FanetAce]
    [ADSBDecoder] as AD2
    [GpsDecoder]
    [GatasConnect] as GC2
    [GatasConnectUDP]
    [RadioTunerRx]
    [RadioTunerTx]
    [AircraftTracker] as AT2
    [Gdl90Service]
    [Bmp280] as BMP2
}

package "Wifi Consumers" {
    [GatasConnect] as GC3
    [GatasConnectUDP] as GU2
    [GDLoverUDP] as GO2
    [DataPort] as DP2
    [Dump1090Client]
    [AirConnect] as AC2
    [Idle] as I2
}

[Idle] --> [ADSBDecoder] : Every5SecMsg
[Idle] --> [WS2] : Every5SecMsg
[Idle] --> [AirConnect] : Every5SecMsg
[Idle] --> [AircraftTracker] : Every5SecMsg
[Idle] --> [DataPort] : Every1SecMsg\nEvery30SecMsg
[Idle] --> [Bmp280] : Every30SecMsg
[Idle] --> [Config] : IdleMsg\nDispatch pending device action

[Config] --> [Sx1262] : ConfigUpdatedMsg
[Config] --> [GDLoverUDP] : ConfigUpdatedMsg
[Config] --> [Ogn1] : ConfigUpdatedMsg
[Config] --> [ADSLAce] : ConfigUpdatedMsg
[Config] --> [FanetAce] : ConfigUpdatedMsg
[Config] --> [AD2] : ConfigUpdatedMsg
[Config] --> [GpsDecoder] : ConfigUpdatedMsg
[Config] --> [StaticGPS] : ConfigUpdatedMsg
[Config] --> [GC2] : ConfigUpdatedMsg
[Config] --> [GatasConnectUDP] : ConfigUpdatedMsg
[Config] --> [RadioTunerRx] : ConfigUpdatedMsg
[Config] --> [RadioTunerTx] : ConfigUpdatedMsg
[Config] --> [AT2] : ConfigUpdatedMsg
[Config] --> [Gdl90Service] : ConfigUpdatedMsg
[Config] --> [BMP2] : ConfigUpdatedMsg
[Config] --> [DP2] : ConfigUpdatedMsg

[GC4] --> [WifiService] : WifiModeRequestMsg
[GC4] --> [Config] : SelectAircraftRequestMsg
[GC4] --> [Config] : DeviceActionRequestMsg\nValidated Bluetooth type 12
[Config] --> [I2] : SystemActionMsg
[WifiService] --> [GC3] : WifiConnectionStateMsg
[WifiService] --> [GU2] : WifiConnectionStateMsg
[WifiService] --> [GO2] : WifiConnectionStateMsg\nAccessPointClientsMsg
[WifiService] --> [DP2] : WifiConnectionStateMsg
[WifiService] --> [Dump1090Client] : WifiConnectionStateMsg
[WifiService] --> [AC2] : WifiConnectionStateMsg
[WifiService] --> [I2] : WifiConnectionStateMsg

@enduml
```

## Active Message Map

| Message | Publisher(s) | Subscriber(s) | Notes |
| --- | --- | --- | --- |
| `GPSSentenceMsg` | `AbstractGnss`, `StaticGPS` | `GpsDecoder`, `DataPort` | Raw or generated NMEA ingress. |
| `OwnshipPositionMsg` | `GpsDecoder` | `Ogn1`, `Flarm2024`, `ADSLAce`, `FanetAce`, `ADSBDecoder`, `AircraftTracker`, `RadioTunerRx`, `RadioTunerTx`, `Gdl90Service`, `DataPort`, `GatasConnect` | Empty notification after `GpsDecoder` atomically replaces `OwnshipState::location`; subscribers load their own snapshot. On each position update, `Gdl90Service` selects fresh pressure samples through `OwnshipState::calculatePressureAltitude()` for report 10; geometric report 11 retains ellipsoid height. |
| `UtcTimeMsg` | `GpsDecoder` | `PicoRtc` | RTC synchronization. |
| `GpsStatsMsg` | `GpsDecoder` | `Ogn1`, `ADSLAce`, `Sx1262`, `Gdl90Service`, `DataPort`, `GatasConnect`, `Idle` | GPS fix and DOP status. |
| `BarometricPressureMsg` | `Bmp280`, `GatasConnect` | `Ogn1` | Empty notification after updating a timestamped ambient-pressure sample in `OwnshipState` and releasing its lock. COBS type 11 is exactly 6 bytes: type u8, pressure u16 in 0.1 hPa (0xffff unavailable), QNH u16 in 0.1 hPa (consumed but ignored), source u8; multi-byte fields are big endian. Sources: Unavailable/Unknown=0, Calculated=1, PressureSensor=2. The existing map retains the accepted sample keyed by measured/calculated method, with 5-second expiry. The update function takes an internal/external parameter (not a model or wire field). BMP280 is internal and COBS is external. It rejects external updates while internal data is fresh; for the same origin it prefers measured over calculated, then unknown-method data. Equal-priority updates replace the sample; stale data may be replaced by the next lower-priority update. Rejected updates are not cached and do not refresh the accepted timestamp. Missing data may invalidate its own accepted origin/method; malformed frames do not update or notify. Pressure altitude is calculated locally using 1013.25 hPa, without QNH or a GPS/QNH fallback. |
| `RadioRxManchesterMsg` | `RxDataFrameQueue` | `Ogn1`, `Flarm2024`, `ADSLAce` | Produced after `Sx1262` receive + Manchester decode. |
| `RadioRxMsg` | `RxDataFrameQueue` | `ADSLAce`, `FanetAce` | Produced after `Sx1262` receive for non-Manchester frames. |
| `IngressAircraftPositionMsg` | `Ogn1`, `Flarm2024`, `ADSLAce`, `FanetAce`, `ADSBDecoder` | `AircraftTracker`, `RadioTunerRx`, `GatasConnect` | Single aircraft decoded from any protocol. |
| `IngressAircraftPositionsMsg` | `ADSLAce` | `AircraftTracker` | Batched ADS-L uplink traffic. |
| `EgressAircraftPositionMsg` | `AircraftTracker` | `Gdl90Service`, `DataPort` | Tracker-selected traffic for outputs. `Gdl90Service` uses `AircraftPositionInfo::pressureAlt` for report 20. Both ownship and traffic pressure-altitude fields use `0xFFF` when unavailable or outside the encoding range; no MSL/ellipsoid fallback is used. |
| `EgressAircraftPositionsMsg` | `AircraftTracker` | `ADSLAce` | Tracker-selected batch for ADS-L transmit. |
| `TrackerStatsMsg` | `AircraftTracker` | `DataPort` | Current number of tracked aircraft after tracker maintenance. |
| `AdapativeRadiusMsg` | `AircraftTracker` | `ADSBDecoder` | Decoder radius feedback. |
| `RadioControlMsg` | `RadioTunerRx` | `Sx1262`, `RadioTunerTx` | Current receive slot / radio assignment. |
| `RadioTxPositionRequestMsg` | `RadioTunerTx` | `Ogn1`, `Flarm2024`, `ADSLAce`, `FanetAce`, `AircraftTracker` | Triggers per-protocol transmit preparation. |
| `RadioTxFrameMsg` | `Ogn1`, `Flarm2024`, `ADSLAce`, `FanetAce` | `Sx1262` | Final frame sent to RF hardware. |
| `DataPortMsg` | `DataPort`, `Bluetooth` | `AirConnect`, `Bluetooth` | NMEA-compatible egress and BLE NMEA ingress. |
| `GdlMsg` | `Gdl90Service` | `GDLoverUDP`, `GatasConnect` | Packed GDL90 bytes for network and bridge outputs. |
| `DeviceActionRequestMsg` | `GatasConnect` COBS parser | `Config` | Bluetooth type-12 action, accepted only for the current 8-bit boot ID; Config stores the latest action in an optional for dispatch on `IdleMsg`. No flash write or reset occurs in the request handler. |
| `GatasConnectTx` | `GatasConnect` | `GatasConnectUDP`, `Bluetooth` | Framed binary output; each configuration broadcast is one type-5 COBS frame carrying the boot ID and dirty flag, without session targeting, including Bluetooth. |
| `GatasConnectRx` | `GatasConnectUDP`, `Bluetooth` | `GatasConnect` | Framed binary input with source transport only. Bluetooth assembles complete frames per connection; the shared parser clears partial frames on transport changes. Type 12 accepts exactly three decoded bytes (type, action, boot ID), only over Bluetooth with a matching boot ID, and bypasses the legacy startup holdback. |
| `SelectAircraftRequestMsg` | `GatasConnect` | `Config` | Typed aircraft selection updates dirty state and retained RAM under the same mutex before ConfigUpdatedMsg. |
| `SystemActionMsg` | `Config` (web handlers and pending device actions dispatched on `IdleMsg`) | `Idle` | Requests watchdog restart or USB boot; Idle owns the platform reset calls. These actions do not implicitly save flash. |
| `WifiModeRequestMsg` | `GatasConnect` | `WifiService` | Companion-originated WiFi mode switch request derived from framed binary control traffic. |
| `WifiConnectionStateMsg` | `WifiService` | `Dump1090Client`, `GDLoverUDP`, `GatasConnect`, `GatasConnectUDP`, `DataPort`, `AirConnect`, `StaticGPS`, `Idle` | Shared network state. |
| `AccessPointClientsMsg` | `WifiService` | `GDLoverUDP` | AP client list for UDP output policy. |
| `IdleMsg` | `Idle` | `Config` | Dispatches the pending device action. Save attempts `configMutex` without waiting and calls `saveToFlash()` if acquired; Restart/USB boot publish `SystemActionMsg`. After execution, clears the optional if it still matches the executed action. A different action queued during execution remains pending; Save also remains pending if the configuration mutex is busy. |
| `Every1SecMsg` | `Idle` | `DataPort` | PFLAU heartbeat. |
| `ConfigUpdatedMsg` | `Config` | `GpsDecoder`, `StaticGPS`, `Bmp280`, `Sx1262`, `Ogn1`, `ADSLAce`, `FanetAce`, `ADSBDecoder`, `AircraftTracker`, `RadioTunerRx`, `RadioTunerTx`, `Gdl90Service`, `GDLoverUDP`, `GatasConnect`, `GatasConnectUDP` | Runtime config propagation. `saveToFlash()` clears dirty state, serializes RAM, writes flash and publishes this notification; GatasConnect refreshes its cached dirty flag through `Configuration::isDirty()` for subsequent type-5 broadcasts. |
| `Every5SecMsg` | `Idle` | `WifiService`, `AirConnect`, `ADSBDecoder`, `AircraftTracker` | Periodic maintenance. |
| `Every30SecMsg` | `Idle` | `Bmp280`, `DataPort` | Slow periodic maintenance. |

## Messages Currently Defined But Not Active In The Main Flow

| Message | Current status |
| --- | --- |
| `ADSBMessageBinMsg` | Supported by `ADSBDecoder`, but the current `Dump1090Client` path calls `ADSBDecoder::receiveBinary(...)` directly instead of publishing to the bus. |
| `Every15SecMsg` | Emitted by `Idle`, but there are no current subscribers. |
| `Every300SecMsg` | Emitted by `Idle`, but there are no current subscribers. |

## Important Non-Bus Paths

- `Dump1090Client -> ADSBDecoder` is a direct call through `BinaryReceiver::receiveBinary(...)`, not a bus message.
- `Sx1262 -> RxDataFrameQueue` uses an internal queue/task handoff before frames become `RadioRxManchesterMsg` or `RadioRxMsg`.
- `Bluetooth` can inject `DataPortMsg` and `GatasConnectRx` into the bus from BLE characteristics.
- `Webserver`, `AceSpi`, and `SerialADSB` are loaded modules, but they are not meaningful bus routers in the current topology.

## Updating This Document

When the topology changes, verify all three of these:

1. Publishers: `rg -n "getBus\\(\\)\\.receive\\(|sendToBus\\(" src/lib src/pico`
2. Subscribers: `rg -n "void on_receive\\(const GATAS::" src/lib`
3. Router declarations: `rg -n "message_router<" src/lib`

Also check for direct module-to-module calls like `receiveBinary(...)`, because those will not appear in a pure bus grep but still affect the effective data flow.
