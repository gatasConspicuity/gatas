# AGENTS.md

This file provides guidance to Codex and other coding agents working in this repository.

## Project Overview

OpenAce is a multi-protocol aviation conspicuity device (OGN, FLARM, ADS-L, FANET, ADS-B) running on Raspberry Pi RP2040/RP2350 with FreeRTOS. It uses a modular message-bus architecture built on the Embedded Template Library (ETL).

## Minimal Changes

Make the smallest readable change that fully solves the task. Start in the code that owns the behavior and reuse existing functions and types. Add a helper, class, file, or configuration option only when the current task needs it. Preserve existing callers and leave unrelated cleanup out of the diff.

## Working Rules

- Prefix every shell command with `rtk`. Use the matching `rtk` subcommand when available, or `rtk run` for commands without a dedicated wrapper.
- Do not touch `src/vendor/` unless the task explicitly requires it.
- When updating `CHANGELOG.md` in preparation for the next release, also update the version badge in `README.md`.
- Prefer `etl::optional<T>` when a function may not produce a value. Avoid boolean return values combined with output references.
- When several values must be returned together and no existing type fits, return a small struct declared next to the API so callers can use `auto result = function();` instead of passing output references.
- Keep desktop test code and embedded code paths aligned when changing shared module behavior.
Updaet agents and changelog- Keep shared code compatible with the C++ standard configured for its desktop tests. The firmware build uses C++20, and many desktop test targets use C++17.
- Design COBS messages as idempotent state notifications, not request/response exchanges. GATAS always provides the required current state; clients must not need to request it or correlate replies with requests or sessions.
- Use fixed-size ETL containers and avoid introducing dynamic-allocation-heavy patterns into firmware code.
- Keep all classes derived from `BaseModule`, directly or indirectly, as stateless as possible. Keep module-owned arrays and maps only when they serve the module's responsibility. Pass per-operation values through arguments, return values, or per-call callback context. Never use member variables as intermediate storage between calls, handlers, or callbacks.
- Modules must not retain a reference or pointer to `Configuration`. Read configuration during construction and synchronously from `ConfigUpdatedMsg`, then copy only the values owned by the module. `Configuration` is not task-safe, so configuration access must remain in these controlled paths.
- Use `float`, never `double`, in project code and tests.
- Prefer ETL text facilities such as `etl::string_stream` and `etl::string_view`; do not use `std::snprintf`.
- Reuse `CoreUtils::addChecksumToNMEA(...)` when assembling NMEA sentences instead of implementing checksum formatting locally.
- Always use braces for control-flow bodies, even when the body is a single line. Put opening and closing braces on their own lines.
- Use explicit status values for firmware errors. The firmware builds with exceptions disabled.
- Document hardware assumptions and units in APIs, configuration, and timing calculations.
- Do not introduce trailing-underscore variable names by default. Prefer the existing local naming style unless a specific file or API already requires a different convention.
- Do not add mutexes or spinlocks around standalone reads or assignments of naturally aligned scalar values up to 32 bits (including bool and 8/16/32-bit values). These accesses are atomic on the RP2040/RP2350. Protect related values together when they must remain consistent as a pair or group; this rule does not cover read-modify-write operations or values wider than 32 bits.
- Prefer `SynchronizedValue` for small shared datasets that need synchronized snapshots or updates. Keep copies and updates short and non-blocking because they run under a spinlock.
- Handle mutexes with RAII `SemaphoreGuard` scopes. Preferred pattern:
  ```cpp
  if (auto guard = SemaphoreGuard(1000, instance->mutex))
  {
      buffer.read(data, ctx.mtu);
  }
  ```
  After the guarded block, RAII releases the mutex. Do not replace this with manual `xSemaphoreTake` / `xSemaphoreGive` pairs unless the task explicitly requires it.
- Use `LwipRAIILock` only around lwIP API calls. It protects lwIP's core context and must not be used as general-purpose synchronization for application state.
- For lwIP packet allocation, use `PBUF_POOL` rather than `PBUF_RAM` (for example, with `pbuf_alloc`).
- When changing message routes or module interactions, update [doc/message-bus.md](doc/message-bus.md) if the documented flow changed.

## Build Commands

### Desktop Tests

Run from the repository root:

```bash
rtk run cmake -S src -B src/build_test -G Ninja
rtk run cmake --build src/build_test
rtk run ctest --test-dir src/build_test --output-on-failure
```

For a focused run, build the affected target and run its registered CTest. For example:

```bash
rtk run cmake --build src/build_test --target coreutils_test
rtk run ctest --test-dir src/build_test -R '^coreutils_test$' --output-on-failure
```

List registered tests with `rtk run ctest --test-dir src/build_test -N`.

### Firmware Build

With `PICO_SDK_PATH` and `FREERTOS_KERNEL_PATH` set, run from the repository root:

```bash
rtk run cmake -S src/pico -B src/pico/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_PLATFORM=rp2040 -DPICO_BOARD=pico_w
rtk run cmake --build src/pico/build
```

For Pico 2 W, use a separate build directory with `-DPICO_PLATFORM=rp2350 -DPICO_BOARD=pico2_w`. The firmware output is a `.uf2` image.

### SystemGUI

The module Monitor helps users check whether a module is working and diagnose common problems. Keep each module’s `getData(...)` output brief. Include a field only if it answers a specific user question, such as “Is the receiver getting packets?” Omit developer-only counters, raw dumps, and duplicate indicators.

From `src/SystemGUI/`, use `rtk npm install` to install dependencies, `rtk npm start` for development, and `rtk npm test` for frontend tests. Set the device IP in `.proxyrc.js` when testing against hardware.

`rtk npm run build` produces `dist/`. To embed UI changes, run `rtk run sh makeFs.sh` **from `src/lib/webserver/`**. The script builds the UI and regenerates `gatas_fsdata.c`, which the firmware build consumes.

### Browser Testing Tools

- `playwright-cli` is installed at `/opt/homebrew/bin/playwright-cli`. Run it through `rtk run playwright-cli ...` for reproducible command-line browser checks.
- `playwright-mcp` is installed at `/opt/homebrew/bin/playwright-mcp`. Prefer an already configured Playwright MCP or browser-control tool when it is exposed to the agent; do not start a duplicate MCP server unnecessarily.
- For SystemGUI work, prefer an existing Chrome tab when the task depends on its live device connection or current UI state. Use Playwright CLI for isolated or repeatable local test runs.

## Architecture

### ACE Module Pattern

For a working example, see [Ogn1](src/lib/ogn/ace/ogn1.hpp) and [its implementation](src/lib/ogn/ace/src/ogn1.cpp). Modules typically derive from `BaseModule` and `etl::message_router`. The constructor receives the bus, configuration and any module-specific dependencies. `GATAS::PostConstruct postConstruct()` checks initialization, then `start()` begins operation. Modules can override `postStart()` for work that depends on other modules having started, and `getData(...)` or `setData(...)` for runtime inspection or control.

### Message Bus Communication

The ETL bus dispatches synchronously in the publisher’s context. A handler that defers work must copy any borrowed data it will need after returning. Prefer message-based communication over direct module-to-module calls.

- Define messages in `src/lib/core/ace/messages.hpp`
- Send with `getBus().receive(FooMsg{...})`
- Subscribe the router with `getBus().subscribe(*this)`. Receive by listing the message in the router template parameters and implementing `on_receive(const FooMsg&)`

Common message groups include:

- Position: `OwnshipPositionMsg`, `IngressAircraftPositionMsg`, `EgressAircraftPositionMsg`
- Radio: `RadioRxManchesterMsg`, `RadioTxPositionRequestMsg`
- Sensors: `GPSSentenceMsg`, `BarometricPressureMsg`
- Timers: `Every5SecMsg`, `Every30SecMsg`

### Configuration

Modules read config through the `Configuration` module:

```cpp
auto val = config.valueByPath(defaultValue, "SectionName", "keyName");
auto str = config.strValueByPath(defaultValue, "SectionName", "keyName");
bool en = config.isModuleEnabled("ModuleName");
```

The firmware build embeds `src/pico/gatas_default_config.json`, with an optional `src/pico/gatas_local_config.json` overlay. The top-level `modules` value is a comma-separated string of module names.

### Hardware Abstraction Base Classes

- `Radio`: radio transceiver base with lookup names `_Radio_0` through `_Radio_3`
- `SpiModule`: SPI interface, `_SPI_0` and `_SPI_1`
- `RtcModule`: RTC abstraction

Use Pico SDK APIs for peripheral and hardware access. Keep interrupt handlers short and use only non-blocking, allocation-free operations in them. When handing off work between one producer and one consumer, prefer the existing ETL `queue_spsc_atomic` pattern where it fits.

### Memory Model

Firmware code favors fixed-size containers and predictable memory behavior. Prefer ETL types such as `etl::unordered_map`, `etl::string`, and `etl::vector` with explicit compile-time limits. Pass large read-only values by const reference and use a span for borrowed buffers. Account for alignment and byte order when reading or writing wire formats.

## Aircraft Timestamp Model

- Use `CoreUtils::timeUs32Raw()` for short elapsed-time measurements independent of PPS, `CoreUtils::monotonic()` for 64-bit monotonic timing, and `CoreUtils::timeUs32()` for PPS-aligned timestamps. Avoid busy-wait loops except where hardware requires one.
- `AircraftPositionInfo.timestamp` is a local relative timestamp in microseconds and should be in the `CoreUtils::timeUs32()` frame.
- Received aircraft packet timestamps represent the remote send time, not the local receive time. By the time a packet is received, the restored packet timestamp is expected to be less than or equal to the local 64-bit epoch timestamp from `CoreUtils::msSinceEpoch()`.
- For packet formats that encode only part of the send time, resolve the timestamp as now-or-past, never as a future timestamp. Use a small max-delta window to reject ambiguous or stale data.
- When converting packet send time to `AircraftPositionInfo.timestamp`, compute elapsed time against `CoreUtils::msSinceEpoch()` and convert the elapsed milliseconds to microseconds, and subtract them from `CoreUtils::timeUs32()`.

## GDL90 Nearby-Traffic Altitude Decision

- Use `CoreUtils::resolveTrafficAltitude(...)` for GDL90 traffic. When ownship pressure altitude is unavailable and MSL fallback is enabled (the default), use MSL for both ownship and traffic, even if the target reports pressure altitude. Using ownship geoid separation for nearby traffic is an accepted approximation. Otherwise prefer target pressure altitude, then apply the target–ownship ellipsoid height difference to ownship pressure altitude when available. Revisit this decision if traffic is reported over substantially larger distances or if an aircraft-specific geoid separation becomes available.

## Testing Notes

- Desktop tests use Catch2 with mocks in `src/lib/mocks/` for FreeRTOS, pico-sdk, and hardware APIs.
- Top-level desktop test aggregation is defined in `src/CMakeLists.txt`.
- When changing a module that already has tests, run the relevant existing tests. Add or update focused coverage when a behavior change or regression needs it.
- Keep desktop tests deterministic and independent of the Pico SDK. Use the existing mocks or simple fakes and avoid sleeps, timing assumptions, and direct peripheral access.
- For equality checks in Catch2, put the expected value first, for example `REQUIRE(expected == actual)`. Cover relevant boundary conditions, error paths, and fixed-size buffer limits.

## Key File Locations

| File | Purpose |
|------|---------|
| `src/lib/core/ace/basemodule.hpp` | `BaseModule` and lifecycle interfaces |
| `src/lib/core/ace/messages.hpp` | Core message type definitions |
| `src/lib/gatas_module.cmake` | Template/pattern for new modules |
| `src/pico/main.cpp` | Firmware entry point and module instantiation |
| `src/pico/gatas_default_config.json` | Embedded configuration defaults |
| `src/lib/mocks/` | Desktop test mocks |
| `src/SystemGUI/` | Web Interface |
| `doc/message-bus.md` | Message bus flow documentation |

## Adding a New Module

1. Create `src/lib/<name>/` with `ace/<name>.hpp` and `ace/src/<name>.cpp`
2. Follow an existing module pattern such as `src/lib/ogn/`
3. Add a `CMakeLists.txt` that includes `src/lib/gatas_module.cmake` and calls `gatas_add_module(...)`
4. Add the new subdirectory to `src/lib/CMakeLists.txt`
5. Link the target in `src/pico/CMakeLists.txt` and wire registration, construction, and loading in `src/pico/main.cpp`. Update the JSON defaults if the module needs settings or should be enabled by default.
6. Add or extend desktop tests when the module has logic that can be validated off-target

## Message Bus Diagram Maintenance

Search for the affected message type across headers and implementations, for example `rtk rg RadioTxPositionRequestMsg src/lib src/pico`. Check both publication sites and router subscriptions, then update only the affected PlantUML sections in `doc/message-bus.md`.

## Vendor Dependencies

Vendored libraries live under `src/vendor/` and include ETL, ArduinoJson, FANET, FLARM, gdl90, libcrc, libmodes, and others. External runtime dependencies such as Pico SDK and FreeRTOS are supplied via environment variables for firmware builds.
