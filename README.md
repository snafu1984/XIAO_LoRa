# XIAO ESP32-S3 + Wio-SX1262 Battery Monitor

LoRaWAN-based battery voltage monitoring using the Seeed Studio XIAO
ESP32-S3, Wio-SX1262 and INA226 voltage sensor.

The firmware uses RadioLib 6.6.0 and LoRaWAN OTAA to transmit battery
voltage measurements to The Things Stack (TTN) over the EU868 frequency
band.

## Features

-   LoRaWAN OTAA connection to The Things Stack
-   Battery voltage measurement using the INA226
-   Transmission of battery voltage in volts
-   Configurable uplink interval via LoRaWAN downlink
-   Persistent storage of uplink interval and LoRaWAN nonces
-   ESP32 Light Sleep between uplinks
-   Modular sensor architecture for future sensor extensions

## Hardware

### Required components

-   Seeed Studio XIAO ESP32-S3
-   Seeed Studio Wio-SX1262 LoRaWAN module
-   INA226 voltage sensor module (HW-699)
-   868 MHz antenna
-   Battery to be monitored (nominally 12 V)

### XIAO ESP32-S3 and Wio-SX1262

The XIAO ESP32-S3 and Wio-SX1262 are connected through the intended
board-to-board connector.

  Signal        ESP32-S3 GPIO
  ----------- ---------------
  SPI SCK                   7
  SPI MISO                  8
  SPI MOSI                  9
  NSS / CS                 41
  DIO1                     39
  RESET                    42
  BUSY                     40
  RF switch                38

GPIO38 controls the external RF switch through RadioLib.

Connect the correct 868 MHz antenna before powering or transmitting.

### INA226 connection

The INA226 is connected to the XIAO ESP32-S3 via I²C.

  INA226   XIAO ESP32-S3
  -------- ---------------
  SDA      D4
  SCL      D5
  GND      GND
  VCC      3.3 V

The default I²C address is `0x40`.

The firmware reads the INA226 bus-voltage register directly. The
register resolution is 1.25 mV per bit.

No shunt calibration is required for voltage measurement.

The firmware converts the measured value to volts and uses a resolution
of 0.01 V for LoRaWAN transmission.

**Note:** The INA226 bus-voltage measurement is limited to 36 V. Ensure
the monitored voltage is within the sensor's specified range.

## Software requirements

-   VSCodium or Visual Studio Code
-   PlatformIO extension or PlatformIO Core CLI
-   Arduino framework for ESP32
-   RadioLib 6.6.0

### PlatformIO configuration

The project uses the following configuration:

-   Board: `seeed_xiao_esp32s3`
-   Platform: `espressif32@54.3.20`
-   Framework: Arduino
-   RadioLib: 6.6.0
-   Serial monitor: 115200 baud

Build the firmware:

``` bash
pio run
```

Upload the firmware:

``` bash
pio run -t upload
```

If necessary, specify the serial port:

``` bash
pio run -t upload --upload-port /dev/cu.usbmodemXXXX
```

Open the serial monitor:

``` bash
pio device monitor -b 115200
```

## TTN device configuration

Create an end device in The Things Stack Console with the following
settings:

  Setting               Value
  --------------------- -----------------------------
  Frequency plan        Europe 863--870 MHz (EU868)
  LoRaWAN MAC version   1.1.0
  Regional Parameters   RP001 1.1.0
  Activation            OTAA
  JoinEUI               8 bytes
  DevEUI                8 bytes
  AppKey                16 bytes
  NwkKey                16 bytes

### Device credentials

Copy the example credentials file:

``` bash
cp include/secrets.example.h include/secrets.h
```

Enter the credentials from your TTN end device in `include/secrets.h`.

Keep `secrets.h` private. It is excluded by `.gitignore` and must never
be committed to Git or shared publicly.

The firmware uses the following RadioLib initialization:

``` cpp
node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
```

Enter EUIs as 64-bit hexadecimal values and keys as 32-character
hexadecimal strings. Do not reverse the hexadecimal strings manually.

## Firmware configuration

The main configuration is located in:

``` text
include/config.h
```

### Debug mode

``` cpp
#define DEBUG_MODE 0
```

  Value   Behavior
  ------- --------------------------------------
  `0`     Normal mode with Light Sleep enabled
  `1`     Debug mode with Light Sleep disabled

Debug output is controlled through the `DEBUG_PRINT`, `DEBUG_PRINTLN`
and `DEBUG_PRINTF` macros.

RadioLib debugging is configured separately through compile-time flags
in `platformio.ini`.

### Uplink interval

The default uplink interval is 10 minutes.

``` cpp
constexpr uint32_t defaultUplinkIntervalMinutes = 10UL;
constexpr uint32_t minUplinkIntervalMinutes = 1UL;
constexpr uint32_t maxUplinkIntervalMinutes = 1440UL;
```

The interval can be changed remotely through a TTN downlink.

The configured interval is stored persistently in ESP32 Preferences.

## LoRaWAN payload

### Uplink

-   FPort: `1`
-   Payload length: 4 bytes
-   Byte order: Big-endian

  Bytes   Data type   Description
  ------- ----------- ----------------------------------
  0--1    uint16      Battery voltage in 0.01 V
  2--3    uint16      Uplink interval in whole minutes

Example:

``` text
04 F0 00 0A
```

Decoded values:

  Measurement              Value
  ----------------- ------------
  Battery voltage        12.64 V
  Uplink interval     10 minutes

The voltage is transmitted as an unsigned 16-bit integer. The receiving
decoder divides the value by 100 to obtain volts.

### TTN payload formatter

The JavaScript decoder is located at:

``` text
docs/ttn_payload_formatter.js
```

In The Things Stack Console:

1.  Open the application.
2.  Select **Payload formatters**.
3.  Select **Uplink**.
4.  Set the device to use the **Application formatter**.
5.  Paste the contents of `docs/ttn_payload_formatter.js`.
6.  Save the changes.

The decoded uplink contains:

``` json
{
  "battery_voltage_v": 12.64,
  "uplink_interval_minutes": 10
}
```

### Downlink

-   FPort: `2`
-   Payload length: 3 bytes
-   Byte order: Big-endian

  Byte   Description
  ------ ----------------------------
  0      Command
  1--2   Uplink interval in minutes

Command:

  Value    Description
  -------- ---------------------
  `0x01`   Set uplink interval

Example:

``` text
01 00 05
```

Sets the uplink interval to 5 minutes.

The accepted interval range is 1 to 1440 minutes.

The new interval is stored persistently in ESP32 Preferences.

## Project structure

``` text
XIAO_LoRa/
├── include/
│   ├── config.h
│   ├── secrets.example.h
│   └── secrets.h
├── src/
│   ├── main.cpp
│   └── sensors/
│       ├── Sensor.h
│       ├── SensorManager.h
│       ├── SensorManager.cpp
│       └── ina226/
│           ├── Ina226Sensor.h
│           └── Ina226Sensor.cpp
├── docs/
│   └── ttn_payload_formatter.js
├── platformio.ini
├── LICENSE
└── README.md
```

The sensor interface and SensorManager provide a modular structure for
adding further sensors in the future.

The INA226 implementation is kept separate from the LoRaWAN
communication logic.

## Persistent storage and LoRaWAN nonces

The firmware stores persistent data in ESP32 Preferences using the
namespace:

``` text
xiao-lora2
```

Stored values include:

-   LoRaWAN nonce buffer
-   Associated DevEUI
-   Configured uplink interval

The firmware checks the stored DevEUI before restoring the nonce buffer.

**Important:** Do not routinely erase the ESP32 flash or delete the
stored nonce buffer when continuing to use the same LoRaWAN device
identity. Doing so can cause OTAA join problems because TTN maintains
its own nonce history.

## Power management

The firmware uses ESP32 Light Sleep between uplinks.

The next transmission is scheduled according to the configured uplink
interval and RadioLib's `timeUntilUplink()` result.

Light Sleep is disabled automatically when `DEBUG_MODE` is set to `1`.

## First test

1.  Connect the XIAO ESP32-S3 and Wio-SX1262.
2.  Connect the 868 MHz antenna.
3.  Connect the INA226 to D4 and D5 via I²C.
4.  Connect the INA226 voltage measurement input to the voltage source.
5.  Configure the TTN device and credentials.
6.  Build and upload the firmware.
7.  Check the serial output.
8.  Check the device's Live data in TTN.

For an initial test, the XIAO USB supply can be used as a voltage
source. A reading around 5 V is expected, depending on the actual USB
supply voltage.

For the final application, connect the 12 V battery and verify the
reading against a multimeter.

## License

See the `LICENSE` file for license information.
