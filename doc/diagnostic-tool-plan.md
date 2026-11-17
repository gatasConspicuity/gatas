# OpenAce diagnostic capture tool — planning

## Purpose

Provide a simple action in the SystemGUI for people who cannot see FANET or
ADS-L traffic, or who are unsure whether the OpenAce radios are transmitting.
The result should be a small,
shareable evidence package rather than a live screen recording or a collection
of manually copied module pages. The user should be able to download that
package as JSON and send it to the maintainer.

The first version should answer:

- Which modules are enabled, available, and reporting errors?
- Are receive, transmit, queue, and connection counters changing?
- Is a failure isolated to FANET, ADS-L, a radio, tuner, input, or output
  module?
- Does the device appear idle because nothing is happening, or because a
  module is unavailable or misconfigured?

This is a diagnostic aid, not a guarantee that an RF signal reached another
device. For example, a received-packet counter normally represents accepted
packets, not raw RF energy or packets rejected by CRC. The report must preserve
the protocol and radio identity for every counter; combined totals would hide
which radio path is failing.

## Initial action: three module captures

Add a **Capture diagnostics** action to the Modules/Actions UI. When pressed,
it should:

1. Capture the module overview and the monitoring variables for every currently
   monitorable module.
2. Wait 10 seconds and capture the same data again.
3. Wait another 10 seconds and capture it a third time.

The complete capture therefore spans about 20 seconds from the first to the
third sample. If the intended requirement is 30 seconds of elapsed observation,
use four samples at `t=0`, `t=10`, `t=20`, and `t=30`; the wording should be
settled before implementation. The current proposal keeps the requested three
captures and treats “30 seconds” as the user-facing collection window only if
the timing is adjusted to match it.

Each sample should include:

- capture timestamp and elapsed time;
- module name, enabled state, post-construct status, and availability;
- the complete raw monitoring object returned by the module endpoint;
- request success/failure and response age, where available.

The raw values should be retained. The UI may add a compact interpretation, but
must not replace an absent value with zero or infer a healthy state from a
missing field.

## Downloadable evidence package

The primary hand-off should be a downloadable JSON file containing:

- a schema/report version;
- device identity and firmware/build information when available;
- capture start time, sample timestamps, and elapsed times;
- the module overview for each sample;
- every raw monitoring response, including arrays and structured values;
- request errors, timeouts, and missing modules;
- derived counter deltas and diagnostic hints, clearly separated from raw data.

The derived summary must never replace the snapshots. A maintainer should be
able to inspect exactly what the device returned at each point in time,
recalculate deltas, and compare the report with the relevant module
implementation. The UI may also offer a human-readable view, but JSON is the
canonical attachment for support and troubleshooting.

## Remote GATAS station and range evidence

Local counters cannot answer an important question: *can another GATAS station
see this device's FANET or ADS-L traffic?* If the user is outside the expected
range of their own receiving station, the diagnostic action should optionally
query a GATAS station directory or relay service and add remote evidence to the
JSON report.

The useful workflow would be:

1. Use the device position and time to find stations that are nearby or have
   recently reported coverage in that area.
2. Let the user search observed aircraft by the FANET/ADS-L identity used by
   the protocol. A registration or transponder lookup may be an optional
   enrichment, but it is not the primary identity for this diagnostic.
3. Show which station observed the FANET/ADS-L message, when it was last seen,
   which radio/protocol received it, and any available receive metadata.
4. Store the response and query metadata in the downloadable diagnostic JSON,
   including an explicit “no data” or “not available” result.

This should initially be read-only. A server-mediated directory or relay is
preferable to making the device connect directly to arbitrary stations: it
allows authentication, rate limiting, privacy controls, and a consistent
schema. Direct station access could be a later option for trusted/local
networks.

### Proposed station JSON shape

The exact API is still to be designed, but a response could contain:

```json
{
  "schema": "gatas.radio-observations.v1",
  "query": {
    "latitude": 52.1,
    "longitude": 5.1,
    "radius_km": 50,
    "protocol": "ads-l",
    "identity": "example-protocol-id"
  },
  "stations": [
    {
      "station_id": "example",
      "location": {"latitude": 52.2, "longitude": 5.0},
      "last_seen": "2026-10-04T12:00:00Z",
      "observations": [
        {
          "identity": "example-protocol-id",
          "registration": null,
          "protocol": "ads-l",
          "radio": "Sx1262_0",
          "signal": null,
          "last_seen": "2026-10-04T11:59:52Z"
        }
      ]
    }
  ]
}
```

This is an example contract, not an implementation commitment. Coordinates,
station identifiers, registrations, and aircraft addresses may be sensitive;
the report must state the retention/sharing policy and provide a way to omit or
redact remote data. Registration or transponder matches, if added later, should
be labelled as external lookup results, not treated as proof that the
FANET/ADS-L identity or radio path is correct.

The current firmware already has GATAS Connect and GATAS Connect UDP status
data, including traffic timing and UDP counters. The planning question is
whether to extend that existing path with a directory/observation request, or
to have the SystemGUI call a separate authenticated service. Either way, the
response should preserve source, timestamp, station identity, and an explicit
confidence/availability state rather than returning an unexplained list.

### Candidate service direction

For this diagnostic, the relevant service is our own GATAS server and its
connected FANET/ADS-L stations:

| Service | Useful evidence | JSON/API fit | Important limitation |
| --- | --- | --- | --- |
| Existing GATAS relay/server | FANET/ADS-L station presence, last-seen observations, protocol, radio slot, and relay path | Best fit; we control the schema and can return JSON | A documented station-directory and radio-observation endpoint still needs to be designed |

For the first implementation, use only the existing GATAS service and report
FANET/ADS-L evidence. Generic ADS-B, OGN, and FLARM services should not be used
to claim that our FANET/ADS-L radio is working: they observe different radio
systems and cannot prove our packet was transmitted or received.

The integration should go through a server-side adapter. That keeps API keys,
rate limits, caching, source attribution, and terms-of-use handling out of the
firmware and gives the diagnostic JSON one stable internal schema.

### Optional aircraft identity lookup sites

External identity sites are not the source of truth for this diagnostic. They
may be useful only when an ADS-L observation also carries a compatible aircraft
registration or another identity that the site understands. They are
enrichment sources only; they are not range or radio-performance evidence for
FANET/ADS-L.

| Site | Search | Use |
| --- | --- | --- |
| External registration database | Registration, if present in the ADS-L observation | Optional aircraft information only; the source must be recorded and the result must not be treated as radio evidence |
| GATAS server mapping | ADS-L identity, station, registration, and user-supplied metadata | Preferred source because it can preserve the actual ADS-L identity and its relationship to the observed radio packet |

The diagnostic UI could provide buttons or copied links for these lookups when
the observed identity has the required format. It should label returned
aircraft details as **external identity information**, preserve the source and
lookup time, and allow the user to include or exclude that information from the
JSON sent to the maintainer. FANET/ADS-L protocol identities should remain the
primary keys in the report because they may not have a registration or Mode-S
mapping at all.

All FANET-capable sites in this project are expected to support ADS-L, so the
first design should query and compare ADS-L observations across GATAS stations.
We have not identified a public ADS-L identity directory that can be treated as
authoritative. The design should therefore support a GATAS server-owned
mapping, explicit user-provided identity metadata, or an unresolved identity
rather than silently treating an ADS-L ID as a Mode-S hex code.

### Sites to investigate later

Keep this list for follow-up research. Compatibility with the project's
FANET/ADS-L identities still needs to be verified for each item.

- [FLARMnet](https://www.flarm.com/en/general-aviation/flarmnet/) — device
  radio address and aircraft information.
- [OGN Devices Database](https://ddb.glidernet.org/) — OGN device and
  registration mapping, including JSON download support.
- [OGN live map](https://live.glidernet.org/) — manual device/aircraft search
  and observation display.
- GATAS server station directory — to be designed for ADS-L station presence,
  radio identity, last-seen observations, and registration metadata.
- GATAS ADS-L identity lookup — to be designed if no existing public service
  can resolve the identities used by our stations.

## How to interpret the three samples

For numeric counters, calculate deltas between adjacent samples and across the
whole capture:

- increasing receive counters indicate accepted traffic is being processed;
- increasing transmit counters indicate the module is handing work to its
  transmit path, but do not alone prove that RF energy left the antenna;
- increasing error counters identify a trend and are more useful than a single
  non-zero value;
- queue depth, retry, timeout, and connection fields show whether work is
  blocked downstream;
- unchanged values are meaningful only when the module is enabled, available,
  and the field is a counter expected to change during the test.

For non-numeric values, record changes and report the before/after values. For
arrays, structured objects, and polar/timing data, retain the snapshots and
avoid pretending that a generic numeric delta is meaningful.

The report should use explicit states such as **increasing**, **unchanged**,
**decreasing**, **changed**, **not available**, and **capture failed**. It should
also identify counters that reset or wrap, rather than reporting a large
positive delta.

## Suggested report layout

### Summary

- device and capture timestamps;
- firmware/build identity if the device already exposes it;
- sample timing and missing/failed requests;
- modules with new or increasing errors;
- modules with changing receive/transmit counters;
- modules unavailable or not initialized.

### Module evidence

For every module, show the three samples or a compact before/delta/after view.
Keep the module name and field name intact so a user can attach the report to a
support request and developers can compare it with the module implementation.

### Hints, not verdicts

The tool may offer narrowly scoped hints, for example:

- a radio reports hardware missing;
- a radio is available but has no tuner assignment;
- a transmitter is enabled but has no valid position/GPS input;
- a receive path has no accepted packets during the capture;
- an output connection is absent while upstream traffic is changing.

Hints must cite the observed fields and avoid claiming that an antenna,
frequency, receiver, or remote aircraft is definitely working based only on
software counters.

## Implementation questions to resolve

1. **Timing:** confirm whether “every 10 seconds for 30 seconds, so 3 captures”
   means samples at `0/10/20` (20 seconds elapsed), or a 30-second window with
   samples at `0/15/30`, or four samples at `0/10/20/30`.
2. **Scope:** define whether “all modules” means all modules returned by the
   module overview, all modules in the existing `monitorable` UI list, or every
   module that implements a monitoring endpoint. Prefer the device-reported
   list eventually so the frontend does not become a second registry.
3. **Consistency:** decide whether the device needs a diagnostic batch endpoint
   to obtain one coherent snapshot, or whether sequential module requests are
   acceptable. Sequential requests should record start/end time and failures.
4. **Payload size:** polar plots, timing arrays, and large structured values may
   make a report unnecessarily large. Keep raw data available, but consider a
   summary-first report with an optional full JSON download.
5. **Privacy and sharing:** identify values that may contain network addresses,
   identifiers, or configuration details. Provide a review/download step before
   sharing externally.
6. **Test conditions:** tell the user what to do during capture—remain in the
   same location, keep the device powered, and if possible perform a known
   transmit or receive test. Otherwise an unchanged counter is only evidence
   that no qualifying event was observed.

## Later extensions

- “Start capture” and “Stop capture” for longer investigations;
- a user-entered test note and selected protocol/aircraft expectations;
- export as JSON plus a human-readable HTML/text report;
- comparison of two captures from before and after a configuration change;
- optional device-side snapshot/batch support to reduce request timing skew;
- a support code containing the report schema version and firmware identity.

## Non-goals for the first implementation

- changing radio configuration automatically;
- transmitting a special test packet without an explicit, separately designed
  user action;
- declaring RF coverage, antenna health, or interoperability from counters
  alone;
- replacing the existing per-module live monitor.
