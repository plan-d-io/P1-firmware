# P1-firmware

Firmware for ESP32 devices that read the P1 port of digital electricity meters and forward the data to your own systems: MQTT, Home Assistant, EnergieID or a local HTTP API. Built for the Fluvius meters in Flanders (Belgium), and it also reads other Belgian and Dutch DSMR meters.

This repository holds the source for all my P1 devices. They share one code base: the meter parsing, configuration, web interface, MQTT and update logic are libraries that every device uses.

![](https://github.com/plan-d-io/P1-dongle/wiki/images/P1dongles.jpg)

## Devices

| Device | Hardware | Status | Branch |
|---|---|---|---|
| **P1 dongle** | M5Stack Atom Lite (ESP32-PICO-D4) plugged into the P1 port | Released, version 3.x | `main` |
| **P1 LoRa bridge** | Two LilyGO TTGO T3 v1.6.1 boards (ESP32 + SX1276, 868 MHz): a transmitter at the meter and a receiver on your WiFi, for meters out of WiFi range | In development | `lora` |

The P1 dongle's hardware design, installation guide and update files live in [plan-d-io/P1-dongle](https://github.com/plan-d-io/P1-dongle). The previous generation of the LoRa bridge (2.x) lives in [plan-d-io/P1-LoRa-receiver](https://github.com/plan-d-io/P1-LoRa-receiver) and [plan-d-io/P1-LoRa-transmitter](https://github.com/plan-d-io/P1-LoRa-transmitter).

## Supported meters

- Fluvius digital meters (Flanders), single-phase and three-phase, including the capacity-tariff fields (average and monthly peak demand)
- Other Belgian and Dutch DSMR meters
- Gas meters connected to the electricity meter over M-bus
- Not supported: Luxembourg meters, which encrypt their telegrams

Every telegram is checked against its CRC; a damaged telegram is discarded rather than published.

## Features of the P1 dongle

- Setup and configuration through a web interface, or through a local HTTP API
- MQTT and MQTT over TLS to your own broker
- Native Home Assistant integration through MQTT discovery
- Optional upload to EnergieID
- Privacy first: cloud services, and even internet access, are optional
- Automatic firmware updates (optional), with automatic rollback when a new version fails to start
- Dongles running 2.x update to 3.x over the air and keep their settings and Home Assistant history

## Installing

Most people do not need to build anything. A P1 dongle with firmware installed updates itself (when automatic updates are on), and the [P1-dongle wiki](https://github.com/plan-d-io/P1-dongle/wiki) explains how to build and set up a dongle. Release notes are under [P1-dongle releases](https://github.com/plan-d-io/P1-dongle/releases).

For a blank Atom, `bin/P1-dongle-merged-flash.bin` in the P1-dongle repository is a complete flash image: erase the flash, then write the image at address `0x0`.

## Building from source

The firmware builds with [PlatformIO](https://platformio.org/). The platform is pinned to the [pioarduino](https://github.com/pioarduino/platform-espressif32) release 55.03.37 (Arduino-ESP32 3.3, ESP-IDF 5.5). Arduino IDE is no longer supported.

```
pio run -e dongle              # build the dongle firmware
pio run -e dongle -t upload    # build and flash over USB
pio test -e native             # run the unit tests on your PC
```

The unit tests run on your computer, not on the device, and need a host C++ compiler on your PATH (on Windows, for example MinGW).

**Differences in a build of your own:**

- **No EnergieID.** EnergieID upload needs partner keys that are not part of this repository. Without them the firmware builds and runs normally, without the EnergieID option.
- **Automatic updates start switched off**, so a self-built firmware is not replaced by an official release without you asking for it. You can switch them on in the web interface.

**Two settings must not change:**

- **The partition table** (`p1_dongle.csv`). Dongles in the field received it with their first USB flash, and an over-the-air update cannot change it. A firmware built for a different table cannot be installed on them.
- **The flash mode, DIO at 80 MHz.** The bootloaders of fielded dongles expect it; a QIO build runs, but reads and writes the stored settings and the file system wrongly.

## Repository layout

| Folder | Contents |
|---|---|
| `src/` | The application per device |
| `lib/` | Shared libraries: `P1DSMR` (telegram parsing), `P1Config` (settings), `P1Web` (web interface and HTTP API), `P1Mqtt`, `P1Publish` (payloads and Home Assistant discovery), `P1Eid` (EnergieID), `P1Ota` (updates), `P1Syslog`, `P1Led`, `P1Button` |
| `web/` | Pages of the web interface, embedded into the firmware at build time |
| `test/` | Unit tests |
| `fixtures/` | P1 telegrams used by the tests: real captures (anonymised), telegrams derived from the Fluvius specification, and deliberately broken ones. See `fixtures/README.md` |
| `tools/` | Helper scripts for building and testing, such as a CRC checker for telegram files |

## Issues and contributions

Bug reports, questions and pull requests are welcome in this repository. For a bug, please mention the firmware version, your meter type and, if you can, the log from the dongle's web interface.

I do not provide guarantees or timelines for new functionality, but I am available for specific developments on request.

## Licence

Source available under the [PolyForm Noncommercial License 1.0.0](LICENSE): free for any noncommercial use. For commercial use, please contact me. The licences of the third-party components in the firmware image are listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
