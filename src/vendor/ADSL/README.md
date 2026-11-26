# ADSL Library

ADS-L stands for "Automatic Dependent Surveillance - Light". It is a protocol designed to enable low-cost, low-power devices to transmit position and other data, enhancing situational awareness and safety in aviation. ADS-L is distinct from ADS-B, but compatible in terms of parameter definitions. (e.g. B for broadcast has been deliberately omitted in anticipation of the possibility of network communications). The "light" in ADS-L refers to the use of low-power, low-cost devices, making it an attractive solution for general aviation, and other aircraft not equipped with certified ADS-B installations. ADS-L transmission is not intended to provide any credit during IFR operations except enhancing pilots’ situational awareness.

This library is a c++ implementation header only *no malloc* version designed to handle the full ADSL protocol for embedded systems.

R. van Twisk <github@rvt.dds.nl>

To run the tests you can use `./build.sh` However, since this is a header only library you can just include `adsl.hpp` in your project.

For more information about Please go to the [Website of Easa ADSL](https://www.easa.europa.eu/en/newsroom-and-events/news/be-seen-and-be-aware-publication-new-technical-specification-ads-l-4)

__Specifications used in this implementation__

[ADSL ISSUE 1](https://www.easa.europa.eu/sites/default/files/dfu/ads-l_4_srd860_issue_1.pdf)

[ADS-L 4 SRD-860 Issue 2](https://www.easa.europa.eu/sites/default/files/dfu/2025-09-24_ads-l_4_srd860_issue_2_final.pdf)

## Dependencies

- ETL (Embedded Template Library)
- C++17 or later

## Implementation example
  
  [GATAS Conspicuity ADSL](https://github.com/rvt/openace/tree/initial/src/lib/adsl/ace)

## Notes

- All memory allocations are static, no malloc
- Designed for embedded systems
- Uses template metaprogramming for compile-time optimizations
- Supports various payload sizes through templates

## Best Practices

1. Always check regional compliance using the Zone system
2. Implement proper error handling in your Connector
3. Monitor neighbor table size in dense networks
4. Use appropriate acknowledgment modes based on reliability needs
5. Consider airtime restrictions when sending frequent updates

## Table with O-band vs M-Band LDR and HDR

### ADS-L SRD-860 Issue 2 Packet Framing

Band vs Packet Length (Issue 2)

| Channel | Packet format | Packet Length field | Manchester |
| ------- | ------------- | ------------------- | ---------- |
| M-band | Fixed-size traffic/status | Required | Yes |
| O-band LDR | Fixed legacy format | Not used | No |
| O-band HDR | Variable-size, including traffic uplinks | Required | No |

Rules

- Both channels carry one length byte on air. OpenAce supplies it before Manchester encoding on M-band. On O-band HDR, the SX1262 supplies it in variable-length GFSK mode (datasheet §6.2.3.3).
- O-band LDR uses its separate fixed legacy frame format.
- Traffic uplinks use O-band HDR and require the packet length field.
