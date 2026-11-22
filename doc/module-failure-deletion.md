# Module deletion after failed post-construction

Reviewed 2026-11-11. `canDelete()` applies only immediately after a non-OK
`postConstruct()` result, before `start()`. It does not allow unloading a
successfully initialised or running module. Recheck the opt-in whenever adding
resources or failure returns to a constructor or `postConstruct()`.

The bootstrap already records the failure and conditionally deletes through
`BaseModule *`. Its virtual destructor clears the module pointer without
clearing the failure result. No bootstrap changes were needed in this review.

## Opted in

| Modules | Reason deletion is safe on existing failure paths |
| --- | --- |
| AceSpi, ADSBDecoder | Only failure is mutex allocation, before hardware/decoder setup. |
| ADSLAce, FanetAce | Only failure is mutex allocation; protocol delegates remain inside owned members. |
| AirConnect | Only failure is mutex allocation; TCP listener is still inactive and its destructor handles a null PCB. |
| Dump1090Client | Publishes on the bus without a receiver lookup. Embedded client post-construction is a no-op and autoConnect only sets a flag. |
| GatasConnect | Only failure is timer allocation, leaving no timer or subscription. |
| GatasConnectUDP | Only failure is UDP PCB allocation, before callback registration. |
| RadioTunerTx | Only failure is task creation, leaving no task. |
| RadioTunerRx | Missing radio or failed task creation leaves no task; EventSync destructor releases its event group. |
| RxDataFrameQueue | Failed task creation leaves no task; queue storage and its static mutex are member-owned and have no external users. |
| WifiService | CYW43 init failure occurs before any object callback, task, timer or bus subscription is registered. |
| StaticGPS | PIO is explicitly disabled. Conflict/missing RTC/failed task creation leave no task or DNS request; NtpClient destructor removes its constructor-created UDP PCB and callback. |
| Ogn1 | Packet-size validation occurs before external resource registration. |
| DataPort, Flarm2024, Gdl90Service, GpsDecoder, PicoRtc | Constructors only initialise owned state and postConstruct currently returns OK without external registrations. |
| Bmp280 (existing opt-in) | Failure leaves no callback, task or separately allocated resource referring to the object. GPIO state does not reference its memory. |
| Sx1262, Lr2021 (existing opt-ins) | Failed configuration/dependency/hardware/task setup precedes interrupt registration; no task survives a non-OK return. Hardware state does not reference object memory. |

## Kept opted out

| Modules | Reason |
| --- | --- |
| AircraftTracker | Timer allocation can fail after mutex allocation/registration; default destructor does not release it. |
| GDLoverUDP | A later allocation can fail after allocating PCBs and registering a receive callback with this; default destructor releases neither. |
| Bluetooth | Constructor publishes this in the static instance pointer; default destructor does not clear it. |
| AbstractGnss, L76B, UbloxM8N | PioSerial can publish itself in interruptHandlers and claim PIO resources before failure; default destructor does not undo either. |
| SerialADSB | PioSerial has the same cleanup gap; postConstruct currently ignores its failure and returns OK. |
| Idle | postConstruct installs a repeating timer referring to this without destructor cancellation. Currently always returns OK. |
| Webserver | postConstruct publishes this in a global pointer without destructor cleanup. Currently always returns OK. |
| Config | Bootstrap owns a static Config, not a heap allocation; it is initialised separately and currently always returns OK. |

Abstract provider classes (RtcModule, SpiModule, Radio,
Configuration and AbstractTransceiver) retain the conservative base default;
concrete subclasses must opt in individually. Test mocks are not boot modules.

StaticGPS also contains an existing `vTaskDelete(staticTaskHandle)` in its
parent-failure branch. With its current constructor (PIO disabled) and the
preceding successful lookup of the same RTC dependency, that branch is not
reachable during the serial bootstrap. If these assumptions change, fix that
branch before retaining the opt-in: deleting a null FreeRTOS task handle deletes
the calling task. This review does not change lifecycle implementation.

## Validation

Desktop tests and the existing RP2040 firmware build were run. A regression test
forces ADSBDecoder mutex allocation failure, deletes through BaseModule, checks
that failure status survives with a null registry pointer, and publishes a
message afterwards. This is source-level lifecycle review and desktop/build
validation, not hardware fault-injection validation of every allocation failure.
