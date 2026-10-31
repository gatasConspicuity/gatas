# GATAS Conspicuity Device

[![Build GaTas](https://github.com/rvt/openace/actions/workflows/ci.yml/badge.svg)](https://github.com/rvt/openace/actions/workflows/ci.yml)
![version](https://img.shields.io/badge/version-v3.6.0--rc1-green)

[<img src="doc/img/Discord-Logo-Blurple.png" height="45px">](https://discord.gg/J6mXTcWndS)

[📋 Changelog](./CHANGELOG.md) | [🎯 Wiki](https://gatasconspicuity.github.io/gatasDoc/)

The GATAS Conspicuity device is designed for General Aviation pilots flying in areas where multiple
protocols, such as OGN, Flarm, ADS-L and FANET, are used. It can transmit and receive multiple
protocols simultaneously (excluding sending ADS-B) using one or more transceiver modules. All
received traffic is sent to your Electronic Flight Bag (EFB), such as SkyDemon, via the GDL90
protocol via WIFI or NMEA via bluetooth or WIFI.

The device is built around the Raspberry RP2040/RP2350 and can be configured with a custom PCB that
supports either two transceivers or a simpler configuration with one with modules from Waveshare
which makes it more of a plug-and-play. In both setups, it can send and receive all protocols using
time-sharing technology. The device can store configurations for multiple aircraft, which can be
selected through an easy-to-use web interface or via gatasServer

Powered by a Li-Ion battery, the device includes a PCB with a USB-C charger. The estimated battery
life is between 6 and 10 hours, though this is subject to further testing.

For FLARM it's current range has been t ested up to 24Km (see below screenshots) but on average a
good 15Km can be expected. This will give you plenty of time to avoid the traffic. EFBs like
SkyDemon can also send out audible traffic warnings.

Note: Although it's primary design is based around GA traffic, there is nothing in the hardware to
prevent other use cases like gliders, paragliders and hang gliders. The goal of the project is to be
seen by transmitting different protocols and be able to see them.

| ![GATAS Server](doc/img/gatasServer.png)<br>Additional traffic via gatasServer                                                       | ![Sky Demon)](doc/img/skydemon-1.png) <br> soaring planes directly via FLARM                |
|--------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------|
| ![Showing Range](doc/img/web-showing-range.png) Showing range of 24Km                                                                | ![Sky Demon)](doc/img/firstversion.jpeg)<br> Two Tranceiver Version I have been flying with |
| ![Gatas Server](doc/img/gatasServer_sel.png) <br> GATAS Server selecting Aircraft                                                    | ![All modules](doc/img/gatas-all-modules.png) <br> All modules                              |
| ![GATAS Pulse](doc/img/pulse-components.png)  <br> [GATAS Pulse Build Instructions](https://github.com/rvt/openace/wiki/gatas-Pulse) | ![GATAS Pulse](doc/img/pulse-aquila.png) <br/> GATAS Pulse                                  |

> [!NOTE]
> The screenshot of SkyDemon was taken from the car while driving for safety reasons. The screenshot
> of the web interface showing the 24Km was taken while parked and the device placed on top of the
> car
> for better air-2-air visibility.

## ADS-L Issue 2 — Ground Station Mode & Traffic Uplink

**New in 2.0.0**: GATAS now supports **ADS-L Issue 2** including ground station mode and traffic uplink. In ground
station mode the device not only transmits its own position but also re-broadcasts received traffic
back into the ADS-L network as uplink frames, making nearby traffic visible to other ADS-L
participants that would otherwise be out of range. This includes any traffic received via Gatas Connect and thus showing additional MLAT traffic.
This is ideal if you have glider fields and want to inform nearby traffic that a fields is active.
A maximum of 10 aircraft closest to the groundstation will be relayed.

<img src="doc/img/groundstation.png" width="600">
Base station mode is active and relaying additional traffic.

## Radio Protocol Support

Radio Protocol is the method used to communicate with other conspicuity devices

| Radio Protocol.        | Send               | Receive            | Multi Protocol     |
|------------------------|--------------------|--------------------|--------------------|
| OGN                    | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: |
| ADS-L                  | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: |
| FLARM (2024)           | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: |
| FANET                  | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: |
| ADS-B via gatasConnect | :no_entry:         | :heavy_check_mark: | :heavy_minus_sign: |
| PAW (2026 uses ADS-L)  | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark: |

\* Multi Protocol is a feature of GATAS that allows to enable multiple protocols both send and
receive on a single transceiver by sharing the air time. The Transceiver will alternate between the
different protocols and prioritize a specific protocol when it receives data for that protocol.

**Adaptive Protocol Prioritisation (New in 2.0.0)**: GATAS now intelligently allocates more listening slots
for protocols that are actively receiving traffic, increasing pings per aircraft and improving overall situational awareness.

- Fanet support sending and receving including forwaring of FANET messages
- gatasConnect is a protocol and service to receive additional traffic information via mobile
  connection.

\* PAW legacy protocol is not supported, only their new ADS-L version

### Communication support

Communication support is what protocols are supported to other devices like electronic flight bags
or other equipment.

| Protocol     | Send UDP over WIFI | Receive UDP over WIFI | Send Bluetooth     | Send TCP over WIFI | Receiver over TCP WIFI |
|--------------|--------------------|-----------------------|--------------------|--------------------|------------------------|
| GDL90        | :heavy_check_mark: | N/A                   | N/A                | N/A                | N/A                    |
| AirConnect   | N/A                | N/A                   | :heavy_check_mark: | :heavy_check_mark: | N/A                    |
| Dump1090     | N/A                | N/A                   | N/A                | N/A                | :heavy_check_mark:     |
| gatasConnect | :heavy_check_mark: | :heavy_check_mark:    | N/A                | N/A                | :heavy_check_mark:     |

\* Sending GDL90 over Bluetooth is not used in the industry so this is currently not setup. If this
is really required, then this can be done. Do let me know the use case

### Tested with Electronic Flight Bag

GATAS is currently tested with SkyDemon only, If you made it work with any other software on Phone
or Tablet, do let me know so I can add or help out to make this work.

| EFB              | GDL90 over WIFI    | Bluetooth          | DataPort (NMEA) WIFI | Note                                                     |
|------------------|--------------------|--------------------|----------------------|----------------------------------------------------------|
| SkyDemon         | :heavy_check_mark: | :heavy_check_mark: | :heavy_check_mark:   | Tested Frequently during flight                          |
| EasyVFR 4        | :heavy_check_mark: |                    |                      |                                                          |
| ForeFlight       | :heavy_check_mark: |                    |                      | With part of ForeFLight extended spec and auto discovery |
| XTrack           |                    | :heavy_check_mark: |                      | Tested for connectivity                                  |
| Air navigation   | :heavy_check_mark: |                    |                      | Tested for connectivity                                  |
| SeeYou Navigator |                    | :heavy_check_mark: |                      | GPS and traffic targets shown                            |

> [!INFO]
> Thanks to  [PocketFMS / EasyVFR](https://easyvfr4.aero/nl/) for providing a Developer license of EasyVFR 4 to help test GATAS GDL90
>


## gatasConnect & gatasServer

**gatasConnect** and **gatasServer** provide a lightweight UDP-based protocol for receiving
additional traffic information.
This is still somewhat experimental but up an running and usable, it's free for you to use on the pre-configured server. Just enable
'gatasConnect' and ensure GATAS does have internet access via your mobile dev ice. Configure **Client** mode to connect to
your mobile's access point, how cool is that!

> [!TIP]
> gatasConnect uses [https://airplanes.live](https://airplanes.live) to to deliver internet-based traffic to your GATAS
> and into your EFB, including ADS-B Out and MLAT-derived traffic.
> If you have the chance, please consider adding airplanes.live to your existing feed or creating a new one.
> By doing so, you help improve traffic coverage for yourself and for others and contribute to safer flying.
>

## Features

- Online web application to select your configured aircraft
- Eliminates the need to manually connect to GATAS vi WIFI to choose an aircraft, saves time and
  you can focus on flying.
- Still compatible with GATAS aircraft selection via **gatas build in web service**
- Future support for **automatic aircraft selection** based on correlated ADS-B traffic

### Network Usage

- **Transmit:** ~52 bytes/second
- **Receive:** ~1024 bytes/second at 30 aircraft. (supports up to 30 nearby aircraft)

## External Libraries and frameworks used

Most libraries are used 'as-is' Some of them have been slightly modified for performance reasons,
compatibility or other reasons.

1. Raspberry PI PICO
   SDK [https://github.com/raspberrypi/pico-sdk](https://github.com/raspberrypi/pico-sdk)
2. FreeRTOS, GATAS uses tasks and timers and avoids loops and runs in multi-core SMP
   mode. [https://www.freertos.org](https://www.freertos.org)
3. LWiP Pretty cool and sometimes confusing TCP/IP protocol
   suite [https://savannah.nongnu.org/projects/lwip/](https://savannah.nongnu.org/projects/lwip/)
4. ArduinoJSON for loading and storing
   configuration [https://arduinojson.org](https://arduinojson.org)
5. etlcpp pretty awesome library written by John
   Wellbelove [https://www.etlcpp.com](https://www.etlcpp.com)
6. libcrc [https://github.com/lammertb/libcrc](https://github.com/lammertb/libcrc)
7. minmea for parsing NMEA
   sentences [https://github.com/kosma/minmea/](https://github.com/kosma/minmea/)
8. Catch2 for unit testing [https://github.com/catchorg/Catch2](https://github.com/catchorg/Catch2)
8. Easy to use FANET implementation in
   C++ [https://github.com/rvt/FANET](https://github.com/rvt/FANET)

# GATAS Disclaimer

> [!IMPORTANT] > **Important Notice: Use at Your Own Risk**
>
> The GATAS Conspicuity Device is provided "as is," without any guarantees or warranties of any
> kind. By using this device, you acknowledge and agree that:
>
> 1. **No Warranty**: The creators of GATAS make no claims or guarantees regarding the accuracy,
     reliability, or fitness for a specific purpose of the device or its associated software. This
     includes, but is not limited to, communication with OGN, Flarm, ADS-L, or any other protocols.
> 2. **Assumption of Risk**: You assume full responsibility for any and all risks associated with
     the use of GATAS. This includes, but is not limited to, risks related to hardware
     malfunctions, software issues, or incorrect transmission/interpretation of data.
> 3. **Limitation of Liability**: Under no circumstances shall the creators, contributors, or
     affiliates of GATAS be held liable for any direct, indirect, incidental, or consequential
     damages resulting from the use, misuse, or inability to use the device or its software.
> 4. **General Aviation Use**: GATAS is intended for use in General Aviation environments where
     multiple protocols such as OGN, Flarm, and ADS-L are used. It is **not certified** for
     professional or commercial aviation use and should not be relied upon as the sole source of
     traffic or navigation information.
> 5. **Compliance**: Users are responsible for ensuring that the use of GATAS complies with local
     aviation regulations and requirements. It is your responsibility to ensure that the device
     operates within the legal limits and safety standards for your jurisdiction.
> 6. **IFR Use**: GATAS is not intended for usage during an IFR flight.
>
> By using GATAS, you agree to the terms of this disclaimer and acknowledge that you are using the
> device and its software at your own risk.
