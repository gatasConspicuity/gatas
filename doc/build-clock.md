# Firmware clock selection

Select one of three CMake cache values using `GATAS_CLOCK_SPEED`:

| Value | System clock | CYW43 PIO divider |
| --- | --- | --- |
| `Default` | SDK board default, normally 125 MHz on RP2040 and 150 MHz on RP2350 | SDK default (currently 2) |
| `200` | 200 MHz | 3 |
| `266` | 266 MHz | 4 |

`266` is selected for a new build directory on both RP2040 and RP2350.
`Default` explicitly selects the SDK board clock. Other values are rejected.
The divider controls the wireless interface's PIO instruction clock; the actual
SPI clock is half that frequency for the SDK's current two-instruction bit loop.
The 200/266 MHz settings therefore use approximately 33.3/33.25 MHz SPI clocks.

From the repository root, configure an existing build directory with one option:

```sh
rtk run cmake -S src/pico -B src/pico/build -DGATAS_CLOCK_SPEED=Default
rtk run cmake -S src/pico -B src/pico/build -DGATAS_CLOCK_SPEED=200
rtk run cmake -S src/pico -B src/pico/build -DGATAS_CLOCK_SPEED=266
```

Then build the selected configuration:

```sh
rtk run cmake --build src/pico/build
```

For a new Pico 2 W build directory, also supply `-DPICO_BOARD=pico2_w`,
`-DPICO_PLATFORM=rp2350` and your normal SDK/FreeRTOS paths. The existing
`rp2040.sh` and `rp2350.sh` scripts preserve the selection cached in their build
directory when rerun. CMake GUI displays the three allowed values as choices.
To switch an existing build to the new 266 MHz default, configure it with
`-DGATAS_CLOCK_SPEED=266`.

Default leaves the SDK's clock and core voltage unchanged. The 200/266 MHz modes
retain the firmware's existing 1.15 V core setting and 10 ms settling delay,
then change the system clock before peripheral initialization. Startup diagnostics
report the actual clock on either chip. Overclock stability requires testing on
the physical board; the build does not establish stability.
