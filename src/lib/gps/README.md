# Adding Support for a New GPS Type

When integrating a new type of GPS receiver, there are two key requirements to ensure proper functionality:

- **Update Rate**:  
  The GPS must provide a consistent update rate of 2 Hz (2 updates per second). This is currently a fixed expectation within the system due to filtering mechanisms that depend on this rate.

- **Data Format**:  
  The NMEA GGA sentence must report altitude relative to the **WGS84 datum above GEOID**, and include the **geoid separation** field. Accurate vertical positioning relies on this information.

### Additional Notes

While any GNSS constellation is technically supported, it is **recommended** to use receivers that support **GPS and Galileo**, with **SBAS/WAAS** enabled for enhanced accuracy and reliability.

## Static GPS

`StaticGPS` is a disabled-by-default, hardware-free `AbstractGnss` implementation. It reads decimal latitude, longitude, altitude above mean sea level, and an NTP server from configuration. Its own FreeRTOS software timer wakes a dedicated task every 500 ms; the task publishes checksum-valid GLL, RMC, GGA, GSA, and two GSV sentences for a stationary aircraft tracking north.

The generated GGA sentence carries the configured mean-sea-level altitude and derives geoid separation from GATAS's embedded EGM2008 lookup. GGA is required by `GpsDecoder` alongside RMC. GSA reports a 3D fix so the rest of the firmware receives normal GPS-fix status. The module waits for valid epoch time, synchronizes from its configured NTP server after Wi-Fi client connectivity, routes the received fractional-second phase through `RtcModule::ppsEvent()`, retries failed requests, and re-disciplines the software PPS every five minutes.

Once the clock has valid time, an NTP correction greater than 100 ms requires three consecutive measurements within 50 ms of the first before it changes time or PPS. Pending corrections are retried after five seconds; the first synchronization is accepted immediately. `ntpLargeChangesHeld` counts measurements held for confirmation, and `ntpSlew` reports the latest measured correction, including one held for confirmation.
