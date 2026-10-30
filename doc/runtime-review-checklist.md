# Runtime review and working checklist

Reviewed 29 October 2026 against `acdd043` (`Use of SynchronizedValue`).

Focus: normal operating paths, FreeRTOS scheduling, deadlocks, races, stack use, and bounded memory. Firmware source was reviewed without changing its behavior. P1 means fix first; P2 means worthwhile follow-up; P3 means lower-priority setup robustness. “Confirmed” means the source establishes the problem; it does not mean a failure was reproduced on hardware. Measurement items are explicitly marked.

Check an item off after its acceptance check passes. For R3, the checkbox records completed implementation and local checks; outstanding hardware acceptance is tracked separately below. Suggested order: R2–R5, then the flight-path follow-ups and measurements. R1 and R9 belong to setup robustness. Item IDs are retained so existing references remain valid. Some fixes overlap; keep them small and avoid introducing a global message-bus lock.

## Operating assumptions

During flight, GATAS runs its traffic, GNSS, radio, Bluetooth, and network services without HTTP status polling or configuration activity. `getData` is used for configuration and initial monitoring, outside normal flight operation. Diagnostic reads and interactive configuration edits must therefore not be used to justify flight-performance priorities.

Setup can still run against live traffic, so setup-only races and deadlocks remain useful fixes, but their impact and acceptance checks are scoped accordingly. Automated state/configuration broadcasts used by companion transports remain part of normal operation; these are distinct from interactive `getData` calls.

## Execution model to keep in mind

[`ThreadSafeBus::receive`](/Volumes/ext/source/OpenAce/src/lib/core/ace/messagerouter.hpp:42) calls the ETL bus directly. It neither queues messages nor serializes concurrent senders. Handlers execute synchronously on the sender's stack, and two tasks can enter the same module concurrently. Messages can reference local data, so simply making this bus asynchronous would break lifetimes.

This makes caller context part of every handler's contract: a timer sender runs its entire downstream chain in the timer daemon; a network callback runs its downstream chain while holding the lwIP core lock.

## Fix first

- [ ] **R2 · P1 · Repair the ADS-L receive task's stack budget.** Compiler-backed stack risk.

  `RxDataFrameQueue` receives a 512-word / 2,048-byte stack and synchronously invokes protocol decoders. An isolated RP2040 compilation with `-O2 -DNDEBUG -fstack-usage` reports 1,888 bytes for `ADSLAce::adsl_receivedUplinkTraffic` and 560 bytes for `ADSL::Protocol::handleRx`. Those two nested frames already exceed the allocation, before the queue task, routers, and downstream handlers. The existing debug flags report 2,016 and 528 bytes respectively. This is an ordinary uplink reception path.

  **Action:** Reduce the large local position batch, or move scratch storage to a clearly owned context, and provide enough measured receive-task stack. Account for all nested bus handlers. Do not move scratch into a shared module member without first proving single ownership.

  **Acceptance:** Exercise full ADS-L uplink batches with stack checking enabled; record the minimum remaining stack under traffic. Confirm the actual build's call chain and stack report after the fix.

  **Evidence:** [task allocation](/Volumes/ext/source/OpenAce/src/lib/sx1262/ace/src/rxdataframequeue.cpp:15), [decoder call](/Volumes/ext/source/OpenAce/src/lib/adslace/ace/src/adslace.cpp:98), [large local batch](/Volumes/ext/source/OpenAce/src/lib/adslace/ace/src/adslace.cpp:264).

- [x] **R3 · P1 · Complete lwIP locking at task entry points.** Implementation complete; hardware validation pending.

  **Original finding:** The GatasConnect timer publishes `GatasConnectTx`, and `GatasConnectUDP` performs DNS resolution and `udp_sendto` without a core lock. Wi-Fi DHCP/mDNS operations and several startup PCB allocations also call raw lwIP APIs from application tasks. `TcpClient::start` and `TcpListener::start` acquire their guards only after allocating/configuring PCBs. Concurrent lwIP activity can corrupt stack state or fail core-lock assertions.

  **Action:** Audit the complete raw-API sequences and bracket them with `LwipRAIILock` when called outside lwIP callbacks, including PCB creation and setup. Keep guards limited to lwIP operations. Include the normal-operation AirConnect inversion in R6. R1's HTTP diagnostics path remains a separate setup follow-up, per the requested scope.

  **Local implementation (29 October 2026):** Guarded companion UDP DNS/send and PCB setup, GDL UDP PCB setup/send, TCP client/listener creation through setup/cleanup, Wi-Fi DHCP client restart and AP DHCP/DNS server lifecycle, mDNS initialization/service lifecycle, and `httpd_init` startup. Existing NTP task paths already guard DNS/send; its DNS/receive callbacks and the DHCP/DNS server receive handlers run inside lwIP. Companion packet preparation stays outside its send guard. GDL UDP takes one core lock in `transmitBuffer` for the complete destination batch; its private `sendTo` helper relies on that lock. With `GATAS_DEBUG == 1`, `LWIP_ASSERT_CORE_LOCKED` uses the SDK's CYW43 ownership check, enabled after lwIP's initial single-threaded initialization. Also implemented the overlapping R6/R7 fixes below. R4's timer-daemon blocking remains a separate open issue.

  **Acceptance:** Enable core-lock assertions and exercise boot, DNS resolution, AP/client transitions, reconnection, and concurrent UDP/TCP traffic.

  **Local verification:** All 29 registered desktop CTest checks passed, including 47 assertions in the four new TCP/AirConnect cases. The RP2040/Pico W Debug firmware build passed with both core-lock hooks linked. No firmware was flashed; boot, DNS, Wi-Fi transitions, and concurrent transport stress remain hardware validation follow-ups.

  **Recheck (30 October 2026):** After restoring the reviewed zero-wait AirConnect guards and single GDL transmit-batch lock, all 28 executable desktop checks and the RP2040/Pico W Debug firmware build passed. The reused CTest directory also retained a stale `gatas_version_test` registration, which failed because `src/tests/gatas_version_test.cmake` is absent from this checkout; the current top-level CMake source does not register that test.

  **Evidence:** [UDP transmit](/Volumes/ext/source/OpenAce/src/lib/gatasconnect/ace/src/gatasconnectudp.cpp:104), [Wi-Fi DHCP](/Volumes/ext/source/OpenAce/src/lib/wifiservice/ace/src/wifiservice.cpp:203), [mDNS](/Volumes/ext/source/OpenAce/src/lib/wifiservice/ace/src/wifiservice.cpp:478), [TCP startup](/Volumes/ext/source/OpenAce/src/lib/utils/ace/tcpclient.hpp:85), [UDP startup](/Volumes/ext/source/OpenAce/src/lib/gdloverudp/ace/src/gdloverudp.cpp:62).

  The local SDK's `lwip_freertos.c` maps the lwIP core lock to the CYW43 async-context lock. The project links `pico_cyw43_arch_lwip_sys_freertos`. This finding concerns raw APIs; pbuf/memory allocation has separate internal protection. See the [project build configuration](/Volumes/ext/source/OpenAce/src/pico/CMakeLists.txt:197) and the [official API guidance](https://www.nongnu.org/lwip/2_1_x/pitfalls.html).

- [ ] **R4 · P1 · Make the GatasConnect software timer notify a worker.** Confirmed blocking work in a timer callback.

  Its callback serializes configuration and ownship, allocates from the mutex-protected global pool, and synchronously publishes to UDP and Bluetooth. The Bluetooth handler permits a 1,000-ms mutex wait. This runs in the shared timer daemon at priority 15, delaying other timers, including the tracker send timer and StaticGPS trigger. Adding missing lwIP locks alone would add another potential wait here.

  **Action:** Follow the existing tracker/StaticGPS pattern: the callback only notifies a task; task context performs serialization and transport work. Reuse an appropriate existing owner if practical.

  **Acceptance:** Timer callbacks contain no blocking lock or transport path. Hold a transport busy during testing and verify tracker timer notifications continue at their intended cadence.

  **Evidence:** [timer callback](/Volumes/ext/source/OpenAce/src/lib/gatasconnect/ace/src/gatasconnect.cpp:160), [pool mutex](/Volumes/ext/source/OpenAce/src/lib/core/ace/poolallocator.hpp:169), [Bluetooth wait](/Volumes/ext/source/OpenAce/src/lib/bluetooth/ace/src/bluetooth.cpp:204), [timer priority](/Volumes/ext/source/OpenAce/src/lib/include/FreeRTOSConfig.h:134). [FreeRTOS explicitly requires callbacks not to block](https://github.com/FreeRTOS/FreeRTOS-Kernel-Book/blob/main/ch06.md).

- [ ] **R5 · P1 · Synchronize Bluetooth connection reset with transmit producers.** Confirmed buffer race.

  Transmit handlers use `bufferMutex` when appending to per-connection buffers. `createConnection`/`removeConnection` call `activate`/`deactivate` without that mutex; these clear the same queues and reset connection metadata. A disconnect during active traffic can clear a buffer while another core appends to it. `inUse` alone does not make the compound operation safe.

  **Action:** Protect connection activation, deactivation, and buffer mutations with the same short mutex scope. Keep BTstack calls outside that scope because callbacks can be immediate. Recheck send-buffer lifetime across the unlocked notify call.

  **Acceptance:** Repeatedly disconnect/reconnect while GPS, traffic, and configuration broadcasts are active; verify clean buffers and connection handles after every reconnect.

  **Evidence:** [lifecycle calls](/Volumes/ext/source/OpenAce/src/lib/bluetooth/ace/src/bluetooth.cpp:231), [buffer resets](/Volumes/ext/source/OpenAce/src/lib/bluetooth/ace/bluetooth.hpp:96), [transmit producer](/Volumes/ext/source/OpenAce/src/lib/bluetooth/ace/src/bluetooth.cpp:173).

## Flight-path follow-up fixes

- [ ] **R6 · P2 · Fix AirConnect's mutex contract and lock ordering.** Local fix implemented with R3; hardware validation pending.

  **Original finding:** AirConnect creates an ordinary mutex but uses `SemaphoreGuard<true>` throughout. It also continues accessing the client list when acquisition fails. Producers hold this mutex while entering lwIP; accept callbacks enter from lwIP and then wait for this mutex, creating a second inversion. The timeout does not make proceeding without ownership safe.

  **Local implementation:** All client-list writers use checked, non-recursive, zero-wait guards. Accept callbacks return `ERR_MEM` when busy or full, allowing lwIP to abort an unadopted connection; peers must reconnect. A busy NMEA producer skips that sentence and increments `tcpWriteErr`; cleanup retries on the next periodic message. Cleanup waits for the last TCP callback to finish under the core lock before destroying a disconnected entry. HTTP diagnostics are outside this fix's scope.

  **Action:** Match creation and acquisition APIs, check guard success, and avoid waiting for an application mutex from a lwIP callback while another task holds it across a lwIP call. A small owner-controlled outgoing queue is an option if needed.

  **Acceptance:** Concurrent accept/disconnect, GPS output, and five-second cleanup run without unprotected writer access or a blocking lock cycle.

  **Evidence:** [mutex creation and handlers](/Volumes/ext/source/OpenAce/src/lib/airconnect/ace/src/airconnect.cpp:8), [accept handler](/Volumes/ext/source/OpenAce/src/lib/airconnect/ace/src/airconnect.cpp:68).

- [ ] **R7 · P2 · Check TCP state and PCB lifetime under the lwIP lock.** Local fix implemented with R3; hardware validation pending.

  **Original finding:** `TcpClient::write` checks `state` and `pcb` before acquiring the lock. The error/receive callback can close the connection and set `pcb = nullptr` between that check and `tcp_sndbuf`/`tcp_write`. `tcp_client_close` similarly tests the pointer before locking. A normal remote disconnect is enough to expose this.

  **Local implementation:** `write`, close, adopt, and startup check PCB lifetime under the core lock. `flush` returns false for disconnected/null PCBs. Repeated listener starts reuse the existing listener. Desktop transport tests assert raw-API lock ownership and inject a disconnect immediately before write/close/flush acquire the lock; startup failure, reconnect, repeated listener lifecycle, busy/full accepts, failed producer acquisition, and disconnected-client cleanup are covered. These are deterministic mock checks, not a scheduler or hardware stress test.

  **Action:** Acquire the lwIP lock before checking state or using/changing the PCB; keep each lifetime-sensitive sequence together. Audit `flush`, too.

  **Acceptance:** Disconnect a peer repeatedly during writes/reconnects with assertions enabled; no stale/null PCB reaches lwIP.

  **Evidence:** [write](/Volumes/ext/source/OpenAce/src/lib/utils/ace/tcpclient.hpp:133), [close](/Volumes/ext/source/OpenAce/src/lib/utils/ace/tcpclient.hpp:217), [error callback](/Volumes/ext/source/OpenAce/src/lib/utils/ace/tcpclient.hpp:247).

- [ ] **R8 · P2 · Serialize tuner reconfiguration and guarantee recovery from a failed pause.** Confirmed protocol weakness; failure requires overlapping updates or a delayed worker.

  GPS zone changes and configuration changes can reconfigure from different tasks. RX updates `configuredDatasources` before pausing its worker, although the worker reads that vector during periodic reassignment. The single shared acknowledgement bit does not identify which caller owns the pause. Both RX and TX return on pause timeout without ensuring the eventual BLOCK is undone, allowing a delayed worker to remain blocked. Separately, setup diagnostics traverse schedules while they are rebuilt; that reader race is a setup concern, not a flight-load concern.

  **Action:** Give reconfiguration a single owner or serialize callers, pause before mutating shared schedules, and make timeout cleanup restore operation. Snapshot schedule diagnostics. Preserve short, bounded synchronization; avoid holding a spinlock while waiting for acknowledgement.

  **Acceptance:** Under flight-like traffic with the UI closed, exercise GPS-driven zone changes and an injected pause timeout; verify continued RX/TX. Separately, test overlapping protocol edits and schedule reads during ground setup.

  **Evidence:** [RX config handler](/Volumes/ext/source/OpenAce/src/lib/radiotuner/ace/src/radiotunerrx_v2.cpp:186), [RX pause](/Volumes/ext/source/OpenAce/src/lib/radiotuner/ace/src/radiotunerrx_v2.cpp:430), [TX pause](/Volumes/ext/source/OpenAce/src/lib/radiotuner/ace/src/radiotunertx_v2.cpp:220).

- [ ] **R10 · P2 · Make Bluetooth NMEA progress at a small MTU and preserve failed sends.** Confirmed queue behavior.

  `PacketBuffer::read(maxSize)` only removes complete sentences that fit. If the first sentence exceeds the negotiated send size, it returns a zero-length view without removing anything, so later sentences cannot progress. The baseline BLE MTU is much smaller than a typical NMEA sentence. Separately, a successful read removes entries before `att_server_notify`; a failed notification therefore loses those sentences. COBS already has a peek/accepted pattern.

  **Action:** Support partial byte transmission or an explicit oversized-sentence policy, and consume queued bytes only after successful notification. Keep failure retries bounded.

  **Acceptance:** Send full NMEA sentences at the baseline MTU and inject a notification failure; the stream eventually progresses without silent loss from the failed attempt.

  **Evidence:** [NMEA sender](/Volumes/ext/source/OpenAce/src/lib/bluetooth/ace/src/bluetooth.cpp:251), [packet removal](/Volumes/ext/source/OpenAce/src/lib/utils/ace/packetbuffer.hpp:139), [COBS acceptance pattern](/Volumes/ext/source/OpenAce/src/lib/bluetooth/ace/src/bluetooth.cpp:287).

- [ ] **R11 · P2 · Handle task creation failure before advertising a running module.** Confirmed unchecked allocations.

  Several `start` methods ignore `xTaskCreate` results and subscribe handlers anyway. A failed tracker task creation, for example, leaves timer/receive handlers with a null task handle. Release builds disable the malloc-failed hook, making the explicit return checks important. The loader also leaves partially constructed failed modules allocated.

  **Action:** Check task creation consistently and propagate failure through the existing module lifecycle. Only enable producers/subscriptions after the consumer exists. Unwind resources for failed construction deliberately; do not enable the loader's commented-out `delete` until module cleanup is correct.

  **Acceptance:** Inject allocation failures into representative tracker, GNSS, and transport startup paths; status reports failure and no callback targets an absent task. Recovered memory matches the resources actually created.

  **Evidence:** [tracker start](/Volumes/ext/source/OpenAce/src/lib/aircrafttracker/ace/src/aircrafttracker.cpp:32), [GNSS start](/Volumes/ext/source/OpenAce/src/lib/gps/ace/src/AbstractGnss.cpp:73), [release hooks](/Volumes/ext/source/OpenAce/src/lib/include/FreeRTOSConfig.h:118), [loader failure](/Volumes/ext/source/OpenAce/src/pico/main.cpp:323).

## Setup and initial monitoring follow-ups

- [ ] **R1 · P2 · Break the tracker/lwIP lock cycle during initial monitoring.** Confirmed lock inversion; setup/monitoring only.

  The tracker takes `trackedAircraftMutex` around its task processing and publishes egress traffic while holding it. `DataPort` converts that to NMEA; `AirConnect` can then call `TcpClient::write`, which waits for the lwIP lock. In the opposite direction, an HTTP request runs inside lwIP and calls `AircraftTracker::getData`, which waits indefinitely for the tracker mutex. With AirConnect active, opening tracker diagnostics during initial monitoring can deadlock both paths. This particular cycle requires `getData`; it does not establish a deadlock during the intended flight operating mode.

  **Action:** Collect bounded output/snapshots under the tracker mutex, release it, then publish messages and format diagnostics. Keep networking outside the tracker lock. Avoid fixing this by making the tracker mutex recursive: the cycle crosses tasks.

  **Acceptance:** During a ground setup session, stream tracked traffic to an AirConnect client while repeatedly fetching tracker JSON. Both continue making progress; inspect the resulting code for either lock being held while waiting for the other.

  **Evidence:** [tracker task](/Volumes/ext/source/OpenAce/src/lib/aircrafttracker/ace/src/aircrafttracker.cpp:208), [egress publication](/Volumes/ext/source/OpenAce/src/lib/aircrafttracker/ace/src/aircrafttracker.cpp:258), [tracker diagnostics](/Volumes/ext/source/OpenAce/src/lib/aircrafttracker/ace/src/aircrafttracker.cpp:68), [NMEA route](/Volumes/ext/source/OpenAce/src/lib/dataport/ace/src/dataport.cpp:32), [TCP write](/Volumes/ext/source/OpenAce/src/lib/utils/ace/tcpclient.hpp:144), [HTTP dispatch](/Volumes/ext/source/OpenAce/src/lib/webserver/ace/src/webserver.cpp:253).

- [ ] **R9 · P3 · Synchronize destination edits and diagnostics during setup.** Confirmed incomplete synchronization; interactive setup only.

  `transmitBuffer` snapshots `connectedClients` and `udpPorts`, but iterates `customClients` directly twice. Configuration clears/rebuilds that ETL list under the shared spinlock. The transmit task never takes that lock around its list reads, so an ordinary configuration edit can invalidate its traversal. Diagnostics also iterate client/port containers without their writers' lock. The custom-client mutation requires a configuration edit, and the diagnostic reader requires `getData`; neither is part of the intended flight workload. Client discovery and Wi-Fi state changes remain normal runtime activity and should retain their existing synchronization.

  **Action:** Copy custom clients and related network settings into the bounded destination snapshot; format diagnostics from a snapshot. Keep network calls outside the spinlock.

  **Acceptance:** During ground setup, edit destinations while streaming to multiple clients and reading diagnostics; no invalid iterators or partially rebuilt destination lists.

  **Evidence:** [configuration writes](/Volumes/ext/source/OpenAce/src/lib/gdloverudp/ace/src/gdloverudp.cpp:28), [transmit](/Volumes/ext/source/OpenAce/src/lib/gdloverudp/ace/src/gdloverudp.cpp:246), [diagnostics](/Volumes/ext/source/OpenAce/src/lib/gdloverudp/ace/src/gdloverudp.cpp:163).

## Scheduling, performance, and memory measurements

- [ ] **R12 · P2 · Measure deadline latency before changing priorities.** Measurement task.

  The priority order is broadly understandable: radio hardware above decoders, transport buffering below hardware, and timers highest. However, `Idle` is priority 6 and synchronously runs every periodic subscriber plus deferred flash saves; the tracker is priority 2 despite a stale comment claiming priority 6. The high-priority periodic task can therefore do application work above radio priority 4. Deferred user-requested flash saves belong to setup and must be measured separately from steady flight operation. Equal-priority decoder/tracker/tuner tasks also share CPU, and two cores do not remove lock contention.

  **Action:** With the UI closed and configuration unchanged, capture radio IRQ-to-service latency, RX queue residence time, tracker output jitter, and longest periodic handler under combined radio/BLE/Wi-Fi load. Measure flash-save duration separately during setup. Then lower or split slow housekeeping work if the measurements justify it. Correct the stale tracker-priority comment.

  **Acceptance:** Record a repeatable busy-load baseline and the before/after measurements for any priority change. No blanket priority increase.

  **Evidence:** [Idle priority and periodic work](/Volumes/ext/source/OpenAce/src/lib/idle/ace/src/idle.cpp:9), [deferred flash save](/Volumes/ext/source/OpenAce/src/lib/config/ace/src/config.cpp:751), [stale priority comment](/Volumes/ext/source/OpenAce/src/lib/include/FreeRTOSConfig.h:207).

- [ ] **R13 · P2 · Establish stack headroom for the complete synchronous call chains.** Measurement task, beyond the specific R2 defect.

  The table below shows sizeable local frames. The tracker allocates 4,096 bytes, while `closest10` and its nested selection routine consume about 3,200 bytes together before protocol encoding and routers. COBS parsing can enter from Bluetooth or lwIP and consumes nearly 2 KiB plus its callback/downstream frames. RX schedule rebuilding uses over 1 KiB in a 2-KiB tuner stack. These paths deserve targeted high-water measurements before reducing stacks or adding features.

  **Action:** Enable stack checking in a representative diagnostic build and collect flight-path minimum headroom for busy RF, maximum uplink batches, COBS bursts, and GPS-driven schedule changes with the UI closed. Record configuration/HTTP stack headroom separately during setup. Preserve an explicit margin after the deepest observed call chain.

  **Acceptance:** Store the measured per-task minimums and compiler flags. Distinguish RTOS task stacks from interrupt stacks and static RAM; do not assume a single function's frame is the whole stack requirement.

- [ ] **R14 · P2 · Add enough pool/queue telemetry to tune memory from evidence.** Measurement task.

  The RF receive queue has only two entries shared by up to two radios. The tracker input queue has 16 entries. The global payload pools contain only 8 × 32-byte, 4 × 64-byte, 4 × 212-byte, and 2 × 648-byte blocks; allocations do not fall back to larger classes. A free-heap figure cannot explain exhaustion of one class. Both platforms also reserve two 2,560-byte lwIP malloc blocks used by API responses. Their RAM cost exists during flight, but concurrent API response demand is a setup concern; it is not evidence of flight-time pool pressure. Bluetooth preallocates all three connection contexts even with one client connected.

  **Action:** Record per-class allocation failures/peak occupancy, queue peaks/drops, minimum FreeRTOS heap, libc heap use, and lwIP pool errors. Run a busy combined workload, then adjust only the constrained sizes. Consider the supported BLE connection count if reclaiming RAM is necessary.

  **Acceptance:** Explain every observed flight-workload drop from counters and record RAM cost before/after any increase. Include a slow or disconnected transport client. Exercise concurrent API requests separately as a setup-capacity check; do not enlarge pools for flight based only on that result.

  **Evidence:** [RF queue](/Volumes/ext/source/OpenAce/src/lib/sx1262/ace/rxdataframequeue.hpp:36), [global pools](/Volumes/ext/source/OpenAce/src/lib/core/ace/constants.hpp:62), [allocation policy](/Volumes/ext/source/OpenAce/src/lib/core/ace/poolallocator.hpp:217), [lwIP pools](/Volumes/ext/source/OpenAce/src/lib/include/lwippools.h:15), [Bluetooth contexts](/Volumes/ext/source/OpenAce/src/lib/bluetooth/ace/bluetooth.hpp:36).

- [ ] **R15 · P3 · Validate watchdog recovery through the existing bus path.** Optional hardware validation; no watchdog redesign justified by this review.

  The intended watchdog contract is that normal operation depends on bus delivery. `Idle` feeds the watchdog and then synchronously publishes periodic messages before it can feed again. If that delivery blocks, feeding stops and the watchdog resets the device. The loop also accesses the CYW43 LED through the SDK, so a blocked CYW43/lwIP lock can stop this loop too. The setup-only lock cycle in R1 therefore does not establish that the watchdog will keep feeding; the earlier review overstated that example.

  There is a narrower distinction between a blocked bus call and a stalled worker: the bus dispatches independently in each sending task, and some handlers only notify a worker. For example, the tracker's `Every5SecMsg` handler returns after `xTaskNotify`, without waiting for tracker processing. Successful periodic dispatch alone does not prove that worker completed its work. This is a boundary to validate, not evidence that a normal flight failure currently bypasses the watchdog.

  **Action:** Keep the existing watchdog design. When doing hardware recovery tests, check a blocked periodic handler, a blocked CYW43 path, and a stalled notified worker. Consider additional monitoring only if a practical failure leaves essential traffic stalled while feeding continues.

  **Acceptance:** Document which injected stalls reset within the watchdog timeout and which propagate through existing dependencies. No per-task heartbeat machinery is required to close this review item if the intended recovery behavior is demonstrated.

  **Evidence:** [feed, periodic dispatch, and CYW43 access](/Volumes/ext/source/OpenAce/src/lib/idle/ace/src/idle.cpp:111), [direct bus dispatch](/Volumes/ext/source/OpenAce/src/lib/core/ace/messagerouter.hpp:36), [tracker notification handler](/Volumes/ext/source/OpenAce/src/lib/aircrafttracker/ace/src/aircrafttracker.cpp:59).


## Measured and configured baseline

Task priorities are relative to `tskIDLE_PRIORITY = 0`. Stack bytes below come from explicit application allocations with 4-byte `StackType_t`; optional modules only incur their task stacks when started. SDK tasks have their own configuration and are not all listed here.

| Task | Priority | Allocated stack bytes |
| --- | ---: | ---: |
| FreeRTOS timer daemon | 15 | 4,096, static |
| Application `Idle` / periodic dispatch | 6 | 5,120 |
| CYW43 async context | 5 | 5,120, SDK static storage |
| SX1262, per radio | 4 | 2,048 |
| GNSS receive | 3 | 4,096 |
| GDL UDP sender | 3 | 2,048 |
| Tracker | 2 | 4,096 |
| RF decoder queue | 2 | 2,048 |
| RX tuner / TX tuner, each | 2 | 2,048 |
| FANET / GDL90 service, each | 2 | 2,048 |
| Serial ADS-B | 2 | 1,024 |
| Wi-Fi state task / StaticGPS worker, each | 0 | 2,048 |

RP2040 FreeRTOS heap: 50 KiB; RP2350: 200 KiB. Module `new` allocations use the separate libc heap. Static RTOS stacks and networking pools consume RAM independently of those heap-free figures.

An ARM size probe using the RP2040 build's headers and defines produced:

| Object | Bytes, excluding separately allocated task stacks/RTOS objects |
| --- | ---: |
| Bluetooth, including three fixed connection contexts | 17,096 |
| AircraftTracker, RP2040 six-predictor configuration | 6,232 |
| RadioTunerRx | 1,688 |
| RxDataFrameQueue | 184, excluding payload blocks owned through pool pointers |
| AircraftPositionInfo | 88 |
| Global payload allocator | 2,776 |

Selected GCC `-fstack-usage` results, bytes per individual function frame:

| Function | Existing RP2040 debug command | Same command with `-O2 -DNDEBUG` |
| --- | ---: | ---: |
| ADS-L uplink receive callback | 2,016 | 1,888 |
| ADS-L protocol `handleRx` | 528 | 560 |
| Tracker `closest10` | 1,904 | 1,928 |
| Tracker `adslUplinkTrigger` | 1,136 | 1,272 |
| RX tuner `spreadSecondIndex` | 1,104 | 1,120 |
| COBS stream `handle` | 1,976 | 1,912 |
| COBS `addPosition` callback | 920 | 928 |

These are isolated compilation measurements, not a full Release firmware build or a hardware high-water measurement. Optimization, inlining, platform, and downstream handlers change total call-chain use. The two compiler modes successfully compiled the five inspected source files; an additional optimized RF queue compile and the size probe also succeeded. Existing compilation flags were taken from `src/pico/build/compile_commands.json`; compiler was Arm GNU 15.2.rel1. Temporary objects were written outside the repository.

## Scope and verification limits

Reviewed the message-bus delivery model, module startup, task/timer creation and priorities, tracker output, RF receive/tuner paths, ADS-L/FANET/ADS-B integration, GNSS task flow, Bluetooth buffering/lifecycle, companion UDP/COBS, GDL UDP, AirConnect/TCP, HTTP configuration/diagnostics, pool allocation, and watchdog feeding. Local Pico SDK and FreeRTOS sources were inspected to resolve lock and allocation behavior. Vendor files were not edited.

No firmware was flashed and no concurrent hardware failure was reproduced. Desktop tests were not rerun: their mocks cannot validate these scheduler/lock interactions, and this change only adds the review document. This is a practical runtime review, not a claim that every protocol algorithm or hardware driver has been exhaustively validated.

Existing strengths worth preserving: fixed-capacity ETL storage, owned pool payloads, task notifications for radio work, short `SynchronizedValue` snapshots, and the tracker/StaticGPS timer callbacks that only notify workers. Standalone aligned scalar accesses are not checklist items; synchronization is proposed where ownership, compound state, or container integrity requires it.

One suspected parser race was excluded after checking the SDK: UDP callbacks and BTstack callbacks use the same CYW43 async-context lock in this build, so separate callback tasks do not by themselves establish concurrent COBS parser entry. Preserve that serialization contract if input processing is moved to a worker.
