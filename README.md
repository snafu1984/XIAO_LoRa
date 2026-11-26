# XIAO ESP32-S3 + Wio-SX1262 LoRaWAN (RadioLib)

PlatformIO starter project for the Seeed XIAO ESP32-S3 with the Wio-SX1262 B2B radio board. It uses RadioLib 6.6.0 and LoRaWAN OTAA for EU868 / The Things Stack (TTN).

This project is based primarily on the supplied `xiao-esp32s3-lorawan` repository, with the PlatformIO project layout adapted and RF-switch handling configured through RadioLib. The supplied `xiao-main` repository was also consulted for its SX1262 pin mapping and `setRfSwitchPins()` usage.

## Hardware

Use the XIAO ESP32-S3 and Wio-SX1262 connected through the intended board-to-board connector. Connect the correct 868 MHz antenna before powering or transmitting.

| Signal | ESP32-S3 GPIO |
|---|---:|
| SPI SCK | 7 |
| SPI MISO | 8 |
| SPI MOSI | 9 |
| NSS / CS | 41 |
| DIO1 | 39 |
| RESET | 42 |
| BUSY | 40 |
| RF switch | 38 |

The SX1262 is created as `SX1262 radio = new Module(NSS, DIO1, RESET, BUSY)`. GPIO38 is passed to `radio.setRfSwitchPins()` so RadioLib can control the external RF path during TX/RX.

## PlatformIO setup

Open this folder in VSCodium with the PlatformIO extension installed. Or use the PlatformIO Core CLI:

```bash
pio pkg update
pio run
```

Upload (replace the port with the one shown on your Mac):

```bash
pio run -t upload --upload-port /dev/cu.usbmodemXXXX
```

Serial monitor:

```bash
pio device monitor -b 115200
```

The project pins RadioLib to 6.6.0 and uses the ESP32 platform version 54.3.20.

## TTN device configuration

Create a **new** end device in The Things Stack. This project targets:

- Frequency plan: Europe 863–870 MHz (EU868)
- LoRaWAN MAC version: **1.1.0**
- Regional Parameters version: **RP001 1.1.0** (often shown as 1.1.0-b)
- Activation: OTAA
- JoinEUI: 8 bytes
- DevEUI: 8 bytes
- AppKey: 16 bytes
- NwkKey: 16 bytes

Copy `include/secrets.example.h` to `include/secrets.h` and enter the values from the new TTN device. Keep `secrets.h` private; it is excluded by `.gitignore`.

```bash
cp include/secrets.example.h include/secrets.h
```

The values in `secrets.h` are placeholders and must be replaced. Do not upload firmware with placeholder credentials and do not post the keys in chat or commit them to Git.

**Important:** The code calls `beginOTAA(joinEUI, devEUI, nwkKey, appKey)`. Enter the EUIs as 64-bit hexadecimal values and keys as 32-character hexadecimal strings, exactly as shown by TTN. Do not reverse the strings manually.

## Firmware behavior

- Initializes I2C on D4/D5, the INA226, SPI and the SX1262.
- Configures the external RF switch on GPIO38 through RadioLib.
- Restores the LoRaWAN nonce buffer from ESP32 NVS, associated with the configured DevEUI.
- Performs OTAA with bounded attempts and long retry backoff.
- Reads battery voltage from the INA226 over I2C and sends it as an unsigned 16-bit big-endian value in 0.01 V steps, on FPort 1.
- Requests an uplink interval of 10 minutes, subject to regional duty-cycle / airtime constraints.

The INA226 bus-voltage register is read directly; no shunt calibration is needed for this voltage-only measurement. The firmware reports voltage with 0.01 V resolution.

## TTN payload formatter

The JavaScript decoder is in `docs/ttn_payload_formatter.js`. Add it to the application's uplink payload formatter as a custom JavaScript formatter to decode `battery_voltage_v` and `uplink_interval_minutes` in TTN.

## Persistent state and device identity

RadioLib's LoRaWAN nonce buffer is stored in ESP32 Preferences under namespace `xiao-lora2`, together with the DevEUI. If you change to a different DevEUI, the firmware discards the previous nonce buffer automatically. For the new TTN end device, use a newly generated DevEUI and keys. Do not routinely erase the ESP32 flash or remove the nonce storage while continuing to use the same LoRaWAN identity.

## First test

1. Attach the antenna.
2. Create the new TTN device using LoRaWAN 1.1.0 / EU868.
3. Copy and fill in `include/secrets.h`.
4. Build and flash the project.
5. Open the serial monitor and check for `Radio initialization OK`, `RF switch configured`, and then the OTAA join result.
6. Check the new device's **Live data** in TTN for the join request, join accept, and uplinks.

Do not start repeated rapid join attempts if the join fails. Check the serial error code and TTN Live data first.

## Source repositories

- `xiao-esp32s3-lorawan` (provided archive): primary LoRaWAN implementation, RadioLib 6.6.0, EU868 and nonce persistence.
- `xiao-main` (provided archive): additional reference for the XIAO/Wio-SX1262 SX1262 pin mapping and RadioLib RF-switch configuration.

The firmware retains the original example's behavior and structure; this package is a PlatformIO adaptation, not a claim of independent hardware validation.
