# Runtime review and working checklist

Reviewed 29 October 2026 against `acdd043` (`Use of SynchronizedValue`).

Focus: normal operating paths, FreeRTOS scheduling, deadlocks, races, stack use, and bounded memory. Firmware source was reviewed without changing its behavior. P1 means fix first; P2 means worthwhile follow-up; P3 means lower-priority setup robustness. “Confirmed” means the source establishes the problem; it does not mean a failure was reproduced on hardware. Measurement items are explicitly marked.

Check an item off after its acceptance check passes. For R2 and R3, the checkbox records completed implementation and local checks; outstanding hardware acceptance is tracked separately below. Suggested order: R2–R5, then the flight-path follow-ups and measurements. R1 and R9 belong to setup robustness. Item IDs are retained so existing references remain valid. Some fixes overlap; keep them small and avoid introducing a global message-bus lock.

## Operating assumptions

During flight, GATAS runs its traffic, GNSS, radio, Bluetooth, and network services without HTTP status polling or configuration activity. `getData` is used for configuration and initial monitoring, outside normal flight operation. Diagnostic reads and interactive configuration edits must therefore not be used to justify flight-performance priorities.

Setup can still run against live traffic, so setup-only races and deadlocks remain useful fixes, but their impact and acceptance checks are scoped accordingly. Automated state/configuration broadcasts used by companion transports remain part of normal operation; these are distinct from interactive `getData` calls.

## Execution model to keep in mind

[`ThreadSafeBus::receive`](/Volumes/ext/source/OpenAce/src/lib/core/ace/messagerouter.hpp:42) calls the ETL bus directly. It neither queues messages nor serializes concurrent senders. Handlers execute synchronously on the sender's stack, and two tasks can enter the same module concurrently. Messages can reference local data, so simply making this bus asynchronous would break lifetimes.

This makes caller context part of every handler's contract: a timer sender runs its entire downstream chain in the timer daemon; a network callback runs its downstream chain while holding the lwIP core lock.

## Fix first

- [x] **R2 · P1 · Repair the ADS-L receive task's stack budget.** Implementation complete; hardware high-water validation pending.

  **Original finding:** The review recorded a 512-word / 2,048-byte `RxDataFrameQueue` stack. An isolated RP2040 compilation with `-O2 -DNDEBUG -fstack-usage` reported 1,888 bytes for `ADSLAce::adsl_receivedUplinkTraffic` and 560 bytes for `ADSL::Protocol::handleRx`. Those two nested frames alone exceeded that allocation. The debug flags reported 2,016 and 528 bytes respectively. The current checkout already allocates 1,280 words / 5,120 bytes; that larger allocation predates this fix and is retained.

  **Action:** Reduce the large local position batch, or move scratch storage to a clearly owned context, and provide enough measured receive-task stack. Account for all nested bus handlers. Do not move scratch into a shared module member without first proving single ownership.

  **Local implementation (30 October 2026):** Build the uplink targets directly in the owning `IngressAircraftPositionsMsg`, removing the separate ten-position vector and its copy during synchronous publication. The message gains an empty constructor; the existing copying constructor remains available to other producers. A compile-time check ensures the message can hold the protocol's maximum target count. No shared scratch storage, extra heap allocation, message-route change, or task-stack increase is introduced.

  **Compiler verification:** Arm GNU 15.2.rel1, RP2040/Pico W. Recompiled the affected translation units with their actual `src/pico/build/compile_commands.json` commands plus `-fstack-usage -fcallgraph-info=su`. The Debug module commands use `-Og -g`; `main.cpp` and the FreeRTOS sources additionally end in `-O0`. The optimized comparison appends `-O2 -DNDEBUG`. All figures are bytes per individual function frame.

  | Function | Debug before | Debug after | Optimized before | Optimized after |
  | --- | ---: | ---: | ---: | ---: |
  | ADS-L uplink receive callback | 2,016 | 1,208 | 1,888 | 1,080 |
  | ADS-L protocol `handleRx` | 528 | 528 | 560 | 560 |
  | RF queue task | 136 | 136 | 168 | 168 |
  | Tracker batch ingress | 232 | 232 | 184 | 184 |

  **Call-chain accounting:** The uplink path is queue trampoline → queue task → bus/ADS-L router → ADS-L receive handler → `handleRx` → uplink callback → bus/tracker router → tracker batch ingress. Tracker ingress synchronously updates antenna statistics and copies positions into its queue, then notifies its worker; tracker prediction and egress run later on the tracker task. Account for both nested bus dispatches, rather than summing only decoder frames. Conservative dispatcher allowances from the emitted functions are 232 bytes per pass in Debug and 120 optimized, using the largest emitted weak/template frame where translation units differ. The trampoline contributes 8 bytes, the ADS-L raw receive handler 24/8, and the longest known direct tracker subtree 464/368 including its ingress frame. Together with the queue task, protocol, and uplink callback this gives 2,832 Debug / 2,432 optimized bytes of known-frame accounting after the fix, leaving 2,288 / 2,688 bytes within the existing allocation for remaining runtime/context overhead. These are compiler accounting figures, not measured stack headroom or a complete bound on libc, assembly, indirect calls, or exceptional logging paths.

  **Local checks:** New module tests encode full ten-target ADS-L packets, run them through the real protocol decoder and synchronous bus, verify all targets and their timestamps, repeat full packets, and check ownship filtering and suppression of empty output. All 372 assertions in two new cases, the 28 existing executable desktop checks, and the RP2040/Pico W Debug firmware build passed. The build enables `configCHECK_FOR_STACK_OVERFLOW = 2`; no firmware was flashed. The stale version-script CTest entry described under R3 was excluded from this run. Receive-task hardware high-water measurement remains pending.

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

- [x] **R4 · P1 · Make the GatasConnect software timer notify a worker.** Implementation complete; validation pending.

  Its callback serializes configuration and ownship, allocates from the mutex-protected global pool, and synchronously publishes to UDP and Bluetooth. The Bluetooth handler permits a 1,000-ms mutex wait. This runs in the shared timer daemon at priority 15, delaying other timers, including the tracker send timer and StaticGPS trigger. Adding missing lwIP locks alone would add another potential wait here.

  **Action:** Follow the existing tracker/StaticGPS pattern: the callback only notifies a task; task context performs serialization and transport work. Reuse an appropriate existing owner if practical.

  **Local implementation (30 October 2026):** The timer now only sets a worker notification bit. A dedicated GatasConnect task sends configuration and ownship in order, coalescing pending timer ticks while busy. Configuration is still read synchronously in its update handler; related shared fields are copied under the same spinlock used for worker snapshots. Task and timer allocation failures are reported, and a failed timer start is logged.

  **Local verification:** RP2040/Pico W Debug firmware build passed. All 28 executable desktop CTest checks passed; the cached suite also contains a stale `gatas_version_test` registration whose source file is absent. These tests do not exercise the new worker. Focused GatasConnect worker and failure-path desktop tests, hardware transport contention and tracker cadence, and worker stack high-water measurement remain outstanding.

  **Acceptance:** Timer callbacks contain no blocking lock or transport path. Hold a transport busy during testing and verify tracker timer notifications continue at their intended cadence.

  **Evidence:** [timer callback](/Volumes/ext/source/OpenAce/src/lib/gatasconnect/ace/src/gatasconnect.cpp:160), [pool mutex](/Volumes/ext/source/OpenAce/src/lib/core/ace/poolallocator.hpp:169), [Bluetooth wait](/Volumes/ext/source/OpenAce/src/lib/bluetooth/ace/src/bluetooth.cpp:204), [timer priority](/Volumes/ext/source/OpenAce/src/lib/include/FreeRTOSConfig.h:134). [FreeRTOS explicitly requires callbacks not to block](https://github.com/FreeRTOS/FreeRTOS-Kernel-Book/blob/main/ch06.md).

- [x] **R5 · P1 · Synchronize Bluetooth connection reset with transmit producers.** Local fix implemented; hardware validation remains open below.

  Transmit handlers use `bufferMutex` when appending to per-connection buffers. Previously, `createConnection`/`removeConnection` cleared the same queues without that mutex, allowing a reset to race with an append.

  **Implementation:** Activation and deactivation use the same mutex as transmit producers. Connection slot lookup remains in the BLE task, and BTstack calls remain outside the mutex. The RP2040 firmware build passed. Send-buffer lifetime across the unlocked notify call remains to be verified.

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

- [x] **R8 · P2 · Serialize tuner reconfiguration and guarantee recovery from a failed pause.** Pause-timeout recovery and RX protocol-update ordering fixed; remaining work deferred below.

  **Original finding:** GPS zone changes and configuration changes can reconfigure from different tasks. RX updates `configuredDatasources` before pausing its worker, although the worker reads that vector during periodic reassignment. The single shared acknowledgement bit does not identify which caller owns the pause. Both RX and TX return on pause timeout without ensuring the eventual BLOCK is undone, allowing a delayed worker to remain blocked. Separately, setup diagnostics traverse schedules while they are rebuilt; that reader race is a setup concern, not a flight-load concern.

  **Action:** Give reconfiguration a single owner or serialize callers, pause before mutating shared schedules, and make timeout cleanup restore operation. Snapshot schedule diagnostics. Preserve short, bounded synchronization; avoid holding a spinlock while waiting for acknowledgement.

  **Implemented — failed-pause recovery:** RX and TX now queue `UNBLOCK` before returning on a pause acknowledgement timeout. TX, like RX, processes `UNBLOCK` after `BLOCK`, so release wins when a delayed worker receives both notification bits together. Caller serialization, acknowledgement ownership, zone update ordering, and diagnostic snapshots are deferred to the TODO below.

  **Implemented — RX protocol update ordering:** The configuration handler copies protocols locally, pauses the RX worker, and only then replaces `configuredDatasources`, clears traffic statistics, and rebuilds the schedule. A failed pause leaves the existing protocol list and traffic statistics unchanged.

  - [ ] **TODO · Complete tuner reconfiguration serialization.** Serialize the full pause/update/rebuild/release transaction and associate acknowledgements with their caller. Also commit zone changes only with the corresponding schedule and snapshot schedule containers for diagnostics. Deferred by user decision: simultaneous protocol edits and GPS-driven zone changes are considered rare in the intended operating workflow. These remaining races are accepted for now, not resolved by the fixes above.

  **Local verification:** Desktop tests compile both actual tuner workers and cover pause timeouts with separate/coalesced BLOCK/UNBLOCK delivery, plus RX configuration updates with successful and failed pauses; all 30 assertions passed. The RP2040/Pico W Debug firmware build passed. Hardware contention and overlapping callers have not been validated.

  **Acceptance:** Under flight-like traffic with the UI closed, exercise GPS-driven zone changes and an injected pause timeout; verify continued RX/TX. Separately, test overlapping protocol edits and schedule reads during ground setup.

  **Evidence:** [RX config handler](/Volumes/ext/source/OpenAce/src/lib/radiotuner/ace/src/radiotunerrx_v2.cpp:186), [RX pause](/Volumes/ext/source/OpenAce/src/lib/radiotuner/ace/src/radiotunerrx_v2.cpp:430), [TX pause](/Volumes/ext/source/OpenAce/src/lib/radiotuner/ace/src/radiotunertx_v2.cpp:220).

- [ ] **R10 · P2 · Make Bluetooth NMEA progress at a small MTU and preserve failed sends.** Confirmed queue behavior; deferred for now.

  `PacketBuffer::read(maxSize)` only removes complete sentences that fit. If the first sentence exceeds the negotiated send size, it returns a zero-length view without removing anything, so later sentences cannot progress. The baseline BLE MTU is much smaller than a typical NMEA sentence. Separately, a successful read removes entries before `att_server_notify`; a failed notification therefore loses those sentences. COBS already has a peek/accepted pattern.

  **Action:** Support partial byte transmission or an explicit oversized-sentence policy, and consume queued bytes only after successful notification. Keep failure retries bounded.

  **Acceptance:** Send full NMEA sentences at the baseline MTU and inject a notification failure; the stream eventually progresses without silent loss from the failed attempt.

  **Evidence:** [NMEA sender](/Volumes/ext/source/OpenAce/src/lib/bluetooth/ace/src/bluetooth.cpp:251), [packet removal](/Volumes/ext/source/OpenAce/src/lib/utils/ace/packetbuffer.hpp:139), [COBS acceptance pattern](/Volumes/ext/source/OpenAce/src/lib/bluetooth/ace/src/bluetooth.cpp:287).

- [ ] **R11 · P2 · Handle task creation failure before advertising a running module.** Confirmed unchecked allocations.

  Several `start` methods ignore `xTaskCreate` results and subscribe handlers anyway. A failed tracker task creation, for example, leaves timer/receive handlers with a null task handle. Release builds disable the malloc-failed hook, making the explicit return checks important. The loader also leaves partially constructed failed modules allocated.

  **Action:** Check task creation consistently and propagate failure through the existing module lifecycle. Only enable producers/subscriptions after the consumer exists. Unwind resources for failed construction deliberately; do not enable the loader's commented-out `delete` until module cleanup is correct.

  **Local progress (31 October 2026):** Task creation failures are checked for the tracker, GNSS, GDL-over-UDP, FANET, GDL90 service, Idle, serial ADS-B, Wi-Fi, and the bootstrap tasks. The loader publishes module success after `start()` and preserves a reported start failure. Wi-Fi reserves its task before dependent modules load and starts it in `postStart()`. Failed tracker and UDP transport allocations release their associated task, timer, mutex, or PCB resources as applicable; GNSS and serial ADS-B release PIO resources before interrupts are enabled. StaticGPS stops its worker if GNSS task creation fails. The loader still intentionally retains failed module objects pending a broader ownership/destructor audit.

  **Local verification:** The RP2040/Pico W Debug build passed and all 28 executable desktop CTest checks passed; a stale cached `gatas_version_test` registration was excluded because its source is absent. New desktop tests inject tracker task and timer allocation failures and GDL90 service task allocation failure. GNSS, GDL-over-UDP, Wi-Fi, bootstrap failure injection and recovered-resource counts remain untested, as do all device-level failures.

  **Code review findings — fixed:**

  - [x] **P1 · Preserve startup failures when registering modules.** Read the live status after `start()` through `startAndRegister()`.
  - [x] **P1 · Keep shared networking intact on Wi-Fi worker allocation failure.** Reserve the waiting worker before CYW43 initialization.
  - [x] **P1 · Avoid cleanup of partially allocated PIO resources.** Publish handles only after the state machine and program are acquired.

  **Review fixes (31 October 2026):** The loader now calls `BaseModule::startAndRegister()`, which reads the live registry after `start()` so an earlier registry snapshot cannot overwrite a reported failure. Wi-Fi allocates its waiting worker before CYW43 initialization; worker allocation failure does not initialize or tear down shared networking, and CYW43 initialization failure deletes the reserved worker. PIO allocation now claims a state machine before loading a program and publishes handles only on success, leaving a null PIO on every allocation failure so serial cleanup cannot use partial handles.

  **Review-fix verification:** The RP2040/Pico W Debug build and 28 executable desktop CTest checks passed (the stale version-script registration was excluded). Two new persistent test executables passed 35 assertions covering live startup status and PIO capacity/state-machine exhaustion, fallback, and success. A temporary isolated harness compiled the production Wi-Fi `postConstruct()` method and passed worker failure, CYW43 failure cleanup, and success cases; this does not exercise the CYW43 runtime or scheduler. Device-level failure injection and the other validation gaps above remain open.

  **Acceptance:** Inject allocation failures into representative tracker, GNSS, and transport startup paths; status reports failure and no callback targets an absent task. Recovered memory matches the resources actually created.

  **Evidence:** [tracker start](/Volumes/ext/source/OpenAce/src/lib/aircrafttracker/ace/src/aircrafttracker.cpp:32), [GNSS start](/Volumes/ext/source/OpenAce/src/lib/gps/ace/src/AbstractGnss.cpp:73), [release hooks](/Volumes/ext/source/OpenAce/src/lib/include/FreeRTOSConfig.h:118), [loader failure](/Volumes/ext/source/OpenAce/src/pico/main.cpp:323).

## Setup and initial monitoring follow-ups

- [ ] **R1 · P2 · Break the tracker/lwIP lock cycle during initial monitoring.** Confirmed lock inversion; setup/monitoring only.

  The tracker takes `trackedAircraftMutex` around its task processing and publishes egress traffic while holding it. `DataPort` converts that to NMEA; `AirConnect` can then call `TcpClient::write`, which waits for the lwIP lock. In the opposite direction, an HTTP request runs inside lwIP and calls `AircraftTracker::getData`, which waits indefinitely for the tracker mutex. With AirConnect active, opening tracker diagnostics during initial monitoring can deadlock both paths. This particular cycle requires `getData`; it does not establish a deadlock during the intended flight operating mode.

  **Action:** Collect bounded output/snapshots under the tracker mutex, release it, then publish messages and format diagnostics. Keep networking outside the tracker lock. Avoid fixing this by making the tracker mutex recursive: the cycle crosses tasks.

  **Acceptance:** During a ground setup session, stream tracked traffic to an AirConnect client while repeatedly fetching tracker JSON. Both continue making progress; inspect the resulting code for either lock being held while waiting for the other.

  **Evidence:** [tracker task](/Volumes/ext/source/OpenAce/src/lib/aircrafttracker/ace/src/aircrafttracker.cpp:208), [egress publication](/Volumes/ext/source/OpenAce/src/lib/aircrafttracker/ace/src/aircrafttracker.cpp:258), [tracker diagnostics](/Volumes/ext/source/OpenAce/src/lib/aircrafttracker/ace/src/aircrafttracker.cpp:68), [NMEA route](/Volumes/ext/source/OpenAce/src/lib/dataport/ace/src/dataport.cpp:32), [TCP write](/Volumes/ext/source/OpenAce/src/lib/utils/ace/tcpclient.hpp:144), [HTTP dispatch](/Volumes/ext/source/OpenAce/src/lib/webserver/ace/src/webserver.cpp:253).

- [x] **R9 · P3 · Synchronize destination edits and diagnostics during setup.** Local fix implemented; hardware validation pending.

  `transmitBuffer` snapshots `connectedClients` and `udpPorts`, but iterates `customClients` directly twice. Configuration clears/rebuilds that ETL list under the shared spinlock. The transmit task never takes that lock around its list reads, so an ordinary configuration edit can invalidate its traversal. Diagnostics also iterate client/port containers without their writers' lock. The custom-client mutation requires a configuration edit, and the diagnostic reader requires `getData`; neither is part of the intended flight workload. Client discovery and Wi-Fi state changes remain normal runtime activity and should retain their existing synchronization.

  **Action:** Copy custom clients and related network settings into the bounded destination snapshot; format diagnostics from a snapshot. Keep network calls outside the spinlock.

  **Local implementation (31 October 2026):** Configuration builds bounded destination containers before publishing them together under the shared spinlock. Transmission snapshots clients, ports, custom destinations, and Wi-Fi routing state together; diagnostics snapshot clients and ports. Access-point client replacement and Wi-Fi routing updates use the same lock. Network sends and diagnostic formatting happen outside the lock.

  **Local verification:** RP2040/Pico W Debug firmware build and all 28 executable desktop CTest checks passed. The stale cached version-script test was excluded. No dedicated GDL-over-UDP concurrency test or hardware exercise was run.

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
| RF decoder queue | 2 | 5,120 |
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

Original review's GCC `-fstack-usage` results, bytes per individual function frame before R2's fix (the updated ADS-L figures are recorded under R2 above):

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

The original review did not flash firmware, reproduce concurrent hardware failures, or rerun desktop tests. Later implementation checks are recorded under R2 and R3 above; desktop mocks still cannot validate hardware scheduling and lock contention. Outstanding tests are consolidated below. This is a practical runtime review, not a claim that every protocol algorithm or hardware driver has been exhaustively validated.

Existing strengths worth preserving: fixed-capacity ETL storage, owned pool payloads, task notifications for radio work, short `SynchronizedValue` snapshots, and the tracker/StaticGPS timer callbacks that only notify workers. Standalone aligned scalar accesses are not checklist items; synchronization is proposed where ownership, compound state, or container integrity requires it.

One suspected parser race was excluded after checking the SDK: UDP callbacks and BTstack callbacks use the same CYW43 async-context lock in this build, so separate callback tasks do not by themselves establish concurrent COBS parser entry. Preserve that serialization contract if input processing is moved to a worker.

## Consolidated test checklist

These boxes track outstanding validation independently of the implementation checkboxes above. R2, R3, R6, and R7 have local fixes ready for hardware validation. Tests for other fixes should run after their implementation and any required instrumentation are ready. Fault injection, latency tracing, and pool/queue counters may need diagnostic firmware support before a hardware session; preparing that support is a code task, not a manual testing prerequisite for the device operator.

For each completed test, record the date, firmware commit plus any local changes, board, configuration, workload, duration/repetition count, measurements, logs, and pass/fail result. Record actual observations before checking a box; a successful build or absence of a visible crash alone does not establish timing, stack, or memory margins.

### Prepare the test build

- [ ] **Diagnostic configuration.** Use a Debug firmware candidate and verify it enables `GATAS_DEBUG == 1`, the lwIP core-lock hooks, and `configCHECK_FOR_STACK_OVERFLOW = 2`. Make per-task stack minimums and reset/assertion logs available. FreeRTOS stack high-water values are words; multiply by four for bytes on RP2040/RP2350. Identify any additional counters or fault-injection controls needed for the selected tests.
- [ ] **Repeatable workload.** Prepare a source of repeated ten-target ADS-L uplinks, other representative RF/GNSS traffic, and active UDP/TCP/BLE clients. Record traffic rates and client counts so later runs can reproduce the load. Keep the web UI closed and configuration unchanged for normal-operation measurements. Label setup/configuration runs separately and record which board variants were actually tested.
- [ ] **Local regression for the candidate.** Build the desktop tests and run the ADS-L uplink and TCP/AirConnect test executables explicitly, as well as the other registered executable tests. Build the target firmware. Capture failures separately from stale test registrations, including the missing version-script entry described under R3. These checks have passed locally for the fixes above; rerun them when preparing a changed candidate.

### Normal-operation bench tests — web UI closed

- [ ] **R2 · ADS-L receive stack.** Repeatedly receive full ten-aircraft uplinks alongside normal RF/Wi-Fi/BLE activity. Include ownship in some packets and exercise repeated full packets without it. Verify delivery of the expected remaining targets, record the minimum remaining stack for `RxDataFrameQueue`, and capture any stack-overflow assertion or reset. Compare the observed margin with the compiler accounting above; record a justified operating margin rather than treating any positive number as sufficient.
- [ ] **R3 · lwIP entry points and Wi-Fi lifecycle.** Exercise boot, AP mode, client mode, AP/client transitions, DHCP lease acquisition/renewal, mDNS discovery, successful and failed DNS lookups, and reconnection after losing the network. Include both an uncached DNS lookup and cached resolution. Stream UDP and TCP traffic concurrently. Verify no core-lock assertion or stall, and confirm services and traffic recover after each transition.
- [ ] **R6/R7 · AirConnect contention and TCP lifetime.** Stream GPS/traffic output while clients repeatedly connect and disconnect, including disconnects during writes/flushes and five-second cleanup. Exercise a full client list and a busy client-list mutex. Verify no blocking lock cycle, stale/null PCB use, invalid list access, or accumulating abandoned connections. Busy/full accepts may be rejected and retried by the peer; record those rejections and any counted skipped NMEA output rather than assuming every attempt must succeed.
- [ ] **R4 · Timer callback isolation — after the worker fix.** Slow or block a transport while configuration/ownship broadcasts are active. Trace the GatasConnect timer callback and tracker timer notifications. Confirm the callback only notifies its worker, contains no blocking transport/lock work, and tracker notifications continue at their configured cadence. Record the largest notification gap.
- [ ] **R5 · Bluetooth disconnect/reset race — after the lifecycle fix.** Repeatedly disconnect and reconnect BLE clients while GNSS, traffic, and automatic configuration broadcasts are active. Verify connection handles remain valid, queues reset consistently, and old connection data is not sent through a new connection. Capture buffer corruption, stale-handle errors, resets, and resource growth.
- [ ] **R8 · RX/TX tuner recovery — after the reconfiguration fix.** Drive GPS-based zone changes under RF load. Inject a delayed pause acknowledgement and a pause timeout, including an acknowledgement arriving after the caller times out. Verify RX and TX resume and continue receiving/transmitting; neither worker may remain permanently blocked. Record the interruption and recovery times.
- [ ] **R10 · Small-MTU NMEA and failed notifications — after the queue fix.** Use the baseline negotiated BLE MTU with a full-length NMEA sentence at the front of the queue and more sentences behind it. Inject a notification failure, then allow transmission to resume. Compare the received byte stream with the input: the leading sentence and later sentences must progress, and the failed attempt must not silently consume queued bytes. Verify retry handling remains bounded.

### Scheduling, stack, and memory measurements

- [ ] **R12 · Deadline baseline.** With combined RF/GNSS/BLE/Wi-Fi load, measure radio IRQ-to-service latency, receive-queue residence time, tracker output jitter, and the longest periodic handler. Record workload and sampling method. Compare with the configured deadlines and repeat the same workload before and after any priority or scheduling change.
- [ ] **R13 · Stack minima across complete call chains.** Capture per-task minimum remaining stack during busy RF, maximum ADS-L uplinks, tracker output/uplink selection, COBS bursts over UDP and BLE, and GPS-driven RX/TX schedule changes. Include the decoder, tracker, tuner, transport, timer, and CYW43/lwIP contexts actually exercised. Store compiler flags and allocated stack sizes alongside the minimums. Distinguish RTOS task stacks from interrupt stacks and static RAM; document the retained margin for each task.
- [ ] **R14 · Pool, queue, and heap pressure — once counters are available.** Record each payload pool's allocation failures and peak occupancy, RF/tracker/transport queue peaks and drops, minimum FreeRTOS heap, libc heap use, and lwIP pool errors during combined traffic. Include a slow client and a disconnected client. Explain observed drops from the counters, verify resource use recovers or reaches a stable bound, and record RAM cost before/after any capacity adjustment.
- [ ] **R11 · Startup allocation failures — after failure handling is fixed.** Inject task/resource allocation failures in representative tracker, GNSS, and transport startup paths. Verify the module reports failure, no producer/subscription targets a missing worker, and cleanup releases the resources that were actually allocated. Record before/after resource counts. Use deterministic desktop injection where possible, then confirm representative device startup failures.

### Ground setup tests — deferred, separate from normal-operation acceptance

HTTP diagnostics remain lower-priority setup work. These tests are retained for completeness and do not form part of the flight-workload measurements above.

- [ ] **R1 · Tracker diagnostics/AirConnect cycle — after the setup fix.** During a ground setup session, stream tracked traffic to an AirConnect client while repeatedly fetching tracker JSON. Verify both paths keep progressing and inspect the lock scopes for either lock being held while waiting for the other.
- [ ] **R9 · Destination edits and diagnostics — after the snapshot fix.** Edit custom UDP destinations and ports while streaming to multiple clients and reading diagnostics. Verify destination snapshots and diagnostic output remain consistent, without invalid iterators or partially rebuilt lists.
- [ ] **R8 · Overlapping configuration and schedule reads.** Apply overlapping protocol/configuration edits while reading schedules and receiving traffic. Verify callers serialize, diagnostic snapshots stay valid, and RX/TX continue after every update. Record this separately from GPS-driven reconfiguration with the UI closed.
- [ ] **R12/R13 · Configuration/flash-save effects.** Measure user-requested flash-save duration and its effect on service latency. Capture configuration and HTTP task-stack minimums separately from steady normal operation; do not use these setup results to justify flight-path priority changes.
- [ ] **R14 · Concurrent API capacity.** Exercise concurrent API requests alongside live traffic and record API/lwIP allocation failures and recovery. Keep the result separate from normal-operation pool demand; do not enlarge flight-path pools solely to accommodate diagnostic requests.

### Optional recovery validation

- [ ] **R15 · Existing watchdog contract.** Inject a blocked periodic bus handler, a blocked CYW43 path, and a stalled notified worker as separate bench tests. Record the reset reason and elapsed time against the configured watchdog timeout, whether feeding stopped, and which services ceased progressing. A notified-worker stall may behave differently from a blocked synchronous bus call; document the result before proposing any watchdog redesign.
