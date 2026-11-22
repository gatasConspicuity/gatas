

## Class RadioTuner {}

* Module that handles timings of protocols and configures the Radios accordingly to listen and request for sending on the radio frequencies according to the protocols and required maximum power output
* Module keeps track of what protocols traffic is received on and will prioritize the timeslots based on received data.
* Can configure and control 1..N Radio modules. Usually 1 or 2 will be installed


#### General operation




## Class CountryRegulation {}

Class that holds the Zone regulations

Per zone the following information is stored:

 * Frequency
 * Timings when data can be received
 * Delay after CAD (when data was received)
 * Maximum power output in the zone
 * Maximum and minimum time between transmissions

 The class can also decide the current `CountryRegulations::Zone`` to be used  (private method) and what Regulation should be used for a specific protocol.

## Transceiver capabilities

Set `capabilities` under the physical device name, such as `Lr2021_0` or `Sx1262_1`, based on the installed RF path and antenna. Restart to apply this setting. The same list applies to RX and TX. Transmission also requires `txEnabled`, and `Ook1090` is receive-only.

A `capabilities` entry in the selected hardware profile's radio pin map overrides the top-level radio setting. The GATAS Custom profile (`PICO_2RADIO`) enables `Ook1090` on `Lr2021_0` this way.

| Capability | RF profiles |
| --- | --- |
| `Gfsk868` | GFSK in the 868/869 MHz band |
| `Lora868` | LoRa in the 868 MHz band |
| `Gfsk915` | GFSK in the 915 MHz band, including North American profiles starting at 902.2 MHz |
| `Ook1090` | 1090 MHz OOK reception of ADS-B extended squitter (DF17), on LR2021 |

Use comma-separated names, such as `"capabilities": "Gfsk868,Lora868"`. An empty or missing list leaves the radio with zero capabilities. An unknown name invalidates the list and logs a warning.

For ADS-B, add `Ook1090` to `capabilities` on an LR2021 with a suitable 1090 MHz RF path and antenna. Enable `ADSBDecoder` in Modules and set **ADS-B (1090 MHz)** to **RX** in Aircraft Configuration. In JSON, add `{"ADSB": {"mode": "RX"}}` to the active aircraft's `protocols` list. The receive schedule works before a GNSS fix. A radio with other enabled RX profiles time-shares between them. The LR2021 settings follow [SoftRF's DF17 profile](https://github.com/lyusupov/SoftRF/blob/master/software/firmware/source/SoftRF/src/driver/radio/radiolib.cpp).

### Testing LR2021 scheduling without radio traffic

Set `Lr2021::SCHEDULER_ONLY` to `true` in `src/lib/tranceiver/ace/lr2021.hpp` and rebuild. Enable the LR2021 instances in Modules and set their capabilities and `txEnabled` as you would for normal use. Inspect their assignments under **RadioTunerRx** and **RadioTunerTx**. This switch applies to all LR2021 instances. They register normally, discard RX and TX requests, and report `schedulerOnly` in their status. The bootstrap still holds configured chip selects inactive, and SPI setup can pulse the shared reset line.
