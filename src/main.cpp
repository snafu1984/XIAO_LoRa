
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <math.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include "config.h"
#include "sensors/SensorManager.h"

void haltDevice(const char* message);

Preferences store;
SensorManager sensorManager;
uint32_t uplinkIntervalMinutes = defaultUplinkIntervalMinutes;

// --------------------------------------------------
// I2C configuration
// --------------------------------------------------

constexpr uint32_t I2C_FREQUENCY = 100000;

void scanI2CBus() {
  DEBUG_PRINTLN("\nScanning I2C bus...");

  uint8_t devicesFound = 0;

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);

    if (Wire.endTransmission() == 0) {
      DEBUG_PRINTF(
        "I2C device found at address: 0x%02X\n",
        address
      );

      devicesFound++;
    }
  }

  DEBUG_PRINTF(
    "I2C scan complete. Devices found: %u\n",
    devicesFound
  );
}

// --------------------------------------------------
// Uplink interval
// --------------------------------------------------

uint32_t uplinkIntervalSeconds() {
  return uplinkIntervalMinutes * 60UL;
}

void loadUplinkInterval() {
  uint32_t savedSeconds = store.getUInt(
    "interval_s",
    defaultUplinkIntervalMinutes * 60UL
  );

  if (savedSeconds < minUplinkIntervalMinutes * 60UL ||
      savedSeconds > maxUplinkIntervalMinutes * 60UL ||
      savedSeconds % 60UL != 0) {
    DEBUG_PRINTLN("Stored uplink interval is invalid; using default.");
    uplinkIntervalMinutes = defaultUplinkIntervalMinutes;
    return;
  }

  uplinkIntervalMinutes = savedSeconds / 60UL;
}

void saveUplinkInterval(uint32_t minutes) {
  uint32_t seconds = minutes * 60UL;

  size_t written = store.putUInt("interval_s", seconds);

  if (written != sizeof(uint32_t)) {
    haltDevice("ERROR: Could not save uplink interval to flash.");
  }

  uplinkIntervalMinutes = minutes;

  DEBUG_PRINTF(
    "New uplink interval saved: %lu minutes.\n",
    (unsigned long)uplinkIntervalMinutes
  );
}

// --------------------------------------------------
// Downlink processing
// --------------------------------------------------

void processDownlink(
  const uint8_t* payload,
  size_t length,
  const LoRaWANEvent_t& event
) {
  if (event.fPort != commandFPort) {
    DEBUG_PRINTF(
      "Downlink ignored: unexpected FPort %u.\n",
      event.fPort
    );
    return;
  }

  if (length != 3) {
    DEBUG_PRINTF(
      "Downlink ignored: expected 3 bytes, received %u.\n",
      (unsigned int)length
    );
    return;
  }

  if (payload[0] != commandSetInterval) {
    DEBUG_PRINTF(
      "Downlink ignored: unknown command 0x%02X.\n",
      payload[0]
    );
    return;
  }

  uint32_t minutes =
    ((uint32_t)payload[1] << 8) | payload[2];

  if (minutes < minUplinkIntervalMinutes ||
      minutes > maxUplinkIntervalMinutes) {
    DEBUG_PRINTF(
      "Downlink ignored: interval %lu minutes is outside allowed range.\n",
      (unsigned long)minutes
    );
    return;
  }

  DEBUG_PRINTF(
    "Interval command received: %lu minutes.\n",
    (unsigned long)minutes
  );

  saveUplinkInterval(minutes);
}

// --------------------------------------------------
// Error handling
// --------------------------------------------------

void haltDevice(const char* message) {
  Serial.println(message);
  Serial.println(
    "Stopped to avoid unsafe or repeated transmissions."
  );

  while (true) {
    delay(1000);
  }
}

void printResult(const char* message, int16_t state) {
  Serial.printf(
    "%s: %s (%d)\n",
    message,
    stateDecode(state).c_str(),
    state
  );
}

// --------------------------------------------------
// Waiting and power management
// --------------------------------------------------

void waitForNextUplink(uint32_t waitMs) {
  if (waitMs == 0) {
    return;
  }

#if DEBUG_MODE

  DEBUG_PRINTF(
    "\nWaiting without Light Sleep for %lu ms...\n",
    (unsigned long)waitMs
  );

  Serial.flush();

  delay(waitMs);

  DEBUG_PRINTLN("Wait finished.");

#else

  esp_err_t wakeupResult = esp_sleep_enable_timer_wakeup(
    (uint64_t)waitMs * 1000ULL
  );

  if (wakeupResult != ESP_OK) {
    Serial.printf(
      "ERROR: Could not configure Light Sleep timer: %d\n",
      (int)wakeupResult
    );

    delay(waitMs);
    return;
  }

  int64_t sleepStartUs = esp_timer_get_time();

  Serial.printf(
    "Entering Light Sleep for %lu ms...\n",
    (unsigned long)waitMs
  );

  Serial.flush();

  esp_err_t sleepResult = esp_light_sleep_start();

  int64_t sleepEndUs = esp_timer_get_time();

  uint64_t actualSleepUs =
    (uint64_t)(sleepEndUs - sleepStartUs);

  esp_sleep_wakeup_cause_t wakeupCause =
    esp_sleep_get_wakeup_cause();

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);

  if (sleepResult != ESP_OK) {
    Serial.printf(
      "ERROR: Light Sleep failed: %d. Falling back to delay().\n",
      (int)sleepResult
    );

    delay(waitMs);
    return;
  }

  Serial.println("Woke up from Light Sleep.");

  Serial.printf(
    "Actual sleep duration: %llu ms\n",
    (unsigned long long)(actualSleepUs / 1000ULL)
  );

  Serial.printf(
    "Wake-up cause: %d\n",
    (int)wakeupCause
  );

  if (wakeupCause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Wake-up confirmed: timer.");
  } else {
    Serial.println("WARNING: Wake-up was not caused by the timer.");
  }

  Serial.flush();

#endif
}

// --------------------------------------------------
// LoRaWAN nonce persistence
// --------------------------------------------------

void saveNonces() {
  uint8_t* buffer = node.getBufferNonces();

  size_t written = store.putBytes(
    "nonces",
    buffer,
    RADIOLIB_LORAWAN_NONCES_BUF_SIZE
  );

  if (written != RADIOLIB_LORAWAN_NONCES_BUF_SIZE) {
    haltDevice("ERROR: Could not save LoRaWAN nonces to flash.");
  }

  store.putULong64("deveui", devEUI);

  DEBUG_PRINTLN("Join nonces saved to flash.");
}

void restoreNonces() {
  if (!store.isKey("nonces")) {
    DEBUG_PRINTLN("No saved nonces: first run of this firmware.");
    return;
  }

  if (store.getULong64("deveui", 0) != devEUI) {
    DEBUG_PRINTLN(
      "Saved nonces belong to another device identity: discarding."
    );

    store.remove("nonces");
    return;
  }

  size_t length = store.getBytesLength("nonces");

  if (length != RADIOLIB_LORAWAN_NONCES_BUF_SIZE) {
    haltDevice(
      "ERROR: Saved nonce buffer has an unexpected size. "
      "Do not erase it and retry with the same device identity."
    );
  }

  uint8_t buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];

  size_t loaded = store.getBytes(
    "nonces",
    buffer,
    sizeof(buffer)
  );

  if (loaded != sizeof(buffer)) {
    haltDevice("ERROR: Could not read saved LoRaWAN nonces.");
  }

  int16_t state = node.setBufferNonces(buffer);

  if (state != RADIOLIB_ERR_NONE) {
    printResult("Restoring nonces failed", state);

    haltDevice(
      "Saved nonces are invalid or do not match this configuration. "
      "Check device credentials and RadioLib version. "
      "Do not disable TTN nonce checks."
    );
  }

  DEBUG_PRINTLN("Saved join nonces restored.");
}

// --------------------------------------------------
// OTAA join
// --------------------------------------------------

void joinNetwork() {
  if (!decodeLoRaWANKey(appKeyHex, appKey) ||
      !decodeLoRaWANKey(nwkKeyHex, nwkKey)) {
    haltDevice(
      "ERROR: AppKey and NwkKey must each be exactly "
      "32 hexadecimal characters."
    );
  }

  node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);

  restoreNonces();

  const uint8_t maxAttempts = 6;

  for (uint8_t attempt = 0; attempt < maxAttempts; attempt++) {
    DEBUG_PRINTF(
      "\nJoining TTN using OTAA, attempt %u/%u...\n",
      (unsigned int)(attempt + 1),
      (unsigned int)maxAttempts
    );

    int16_t state = node.activateOTAA();

    saveNonces();

    if (state == RADIOLIB_LORAWAN_NEW_SESSION ||
        state == RADIOLIB_LORAWAN_SESSION_RESTORED ||
        (state == RADIOLIB_ERR_NONE && node.isActivated())) {

      DEBUG_PRINTLN("Joined TTN!");

      DEBUG_PRINTF(
        "DevAddr: %08lX\n",
        (unsigned long)node.getDevAddr()
      );

      return;
    }

    printResult("Join attempt failed", state);

    if (attempt + 1 < maxAttempts) {
      uint32_t retrySeconds = 300UL << attempt;

      if (retrySeconds > 3600UL) {
        retrySeconds = 3600UL;
      }

      DEBUG_PRINTF(
        "Retrying in %lu minutes.\n",
        (unsigned long)(retrySeconds / 60UL)
      );

      Serial.println(
        "Check TTN Live data for the actual rejection or downlink error."
      );

      delay(retrySeconds * 1000UL);
    }
  }

  haltDevice(
    "Join attempt limit reached. Check TTN and gateway logs "
    "before restarting the board."
  );
}

// --------------------------------------------------
// Setup
// --------------------------------------------------

void setup() {
  Serial.begin(115200);

#if DEBUG_MODE

  unsigned long started = millis();

  while (!Serial && millis() - started < 10000UL) {
    delay(10);
  }

  DEBUG_PRINTLN("\nXIAO battery-voltage LoRaWAN sensor");
  DEBUG_PRINTLN("Debug mode enabled: Light Sleep disabled.");

#else

  DEBUG_PRINTLN("Normal mode enabled: Light Sleep active.");

#endif

  // Initialize I2C on XIAO ESP32-S3 D4 (SDA) and D5 (SCL).
  if (!Wire.begin(D4, D5, I2C_FREQUENCY)) {
    haltDevice("ERROR: Could not initialize I2C.");
  }

  DEBUG_PRINTLN("I2C initialized.");

#if DEBUG_MODE
  scanI2CBus();
#endif

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);

  DEBUG_PRINTLN("Initializing SX1262...");

  int16_t state = radio.begin(868.1);

  if (state != RADIOLIB_ERR_NONE) {
    printResult("Radio initialization failed", state);
    haltDevice("Check the Wio module connection and radio pins.");
  }

  DEBUG_PRINTLN("Radio initialization OK");

  radio.setRfSwitchPins(LORA_RF_SW, RADIOLIB_NC);

  DEBUG_PRINTLN("RF switch configured (GPIO38).");

  if (!store.begin(STORAGE_NAMESPACE, false)) {
    haltDevice("ERROR: Could not open ESP32 persistent storage.");
  }

  loadUplinkInterval();

  DEBUG_PRINTF(
    "Configured uplink interval: %lu minutes.\n",
    (unsigned long)uplinkIntervalMinutes
  );

  node.setADR(true);
  node.setDutyCycle(true, 1250);
  node.setDeviceStatus(0);

  joinNetwork();

  if (!sensorManager.begin(Wire)) {
    haltDevice("ERROR: INA226 initialization failed. Check wiring and I2C address.");
  }

  DEBUG_PRINTLN("INA226 initialized.");
  DEBUG_PRINTLN("Ready to send battery voltage.");
}

// --------------------------------------------------
// Main loop
// --------------------------------------------------

void loop() {
  uint32_t waitMs = node.timeUntilUplink();

  if (waitMs > 0) {
    DEBUG_PRINTF(
      "Airtime wait: %lu seconds\n",
      (unsigned long)((waitMs + 999UL) / 1000UL)
    );

    waitForNextUplink(waitMs);
  }

  float batteryVoltage = 0.0f;

  if (!sensorManager.readBatteryVoltage(batteryVoltage) ||
      !isfinite(batteryVoltage) ||
      batteryVoltage < 0.0f ||
      batteryVoltage > 65.535f) {
    DEBUG_PRINTLN("Invalid battery voltage: skipping this reading.");
    waitForNextUplink(uplinkIntervalSeconds() * 1000UL);
    return;
  }

  // Uplink payload, big-endian:
  // bytes 0..1: battery voltage in 0.01 V
  // bytes 2..3: configured interval in whole minutes

  uint16_t encodedVoltage =
    (uint16_t)lroundf(batteryVoltage * 100.0f);
  uint16_t encodedInterval = (uint16_t)uplinkIntervalMinutes;

  uint8_t payload[4] = {
    (uint8_t)(encodedVoltage >> 8),
    (uint8_t)(encodedVoltage & 0xFF),
    (uint8_t)(encodedInterval >> 8),
    (uint8_t)(encodedInterval & 0xFF)
  };

  DEBUG_PRINTF(
    "\nSending battery voltage: %.2f V\n",
    batteryVoltage
  );

  DEBUG_PRINTF(
    "Configured interval: %lu minutes\n",
    (unsigned long)uplinkIntervalMinutes
  );

  DEBUG_PRINTF(
    "Payload: %02X %02X %02X %02X, FPort: 1\n",
    payload[0],
    payload[1],
    payload[2],
    payload[3]
  );

  uint8_t downlinkPayload[16] = { 0 };
  size_t downlinkLength = sizeof(downlinkPayload);

  LoRaWANEvent_t downlinkEvent = {};

  uint32_t frameCountBefore = node.getFCntUp();

  int16_t state = node.sendReceive(
    payload,
    sizeof(payload),
    1,
    downlinkPayload,
    &downlinkLength,
    false,
    nullptr,
    &downlinkEvent
  );

  DEBUG_PRINTF("sendReceive() state: %d\n", state);

  DEBUG_PRINTF(
    "downlinkLength: %u\n",
    (unsigned int)downlinkLength
  );

  DEBUG_PRINTF(
    "downlinkEvent FPort: %u\n",
    downlinkEvent.fPort
  );

  DEBUG_PRINTF(
    "downlinkEvent FCnt: %lu\n",
    (unsigned long)downlinkEvent.fCnt
  );

  if (downlinkLength > 0) {
    DEBUG_PRINT("Downlink application payload: ");

    for (size_t i = 0; i < downlinkLength; i++) {
      DEBUG_PRINTF("%02X ", downlinkPayload[i]);
    }

    DEBUG_PRINTLN("");
  }

  saveNonces();

  if (downlinkLength > 0) {
    DEBUG_PRINTLN("Application downlink received!");

    if (downlinkEvent.fPort == commandFPort) {
      processDownlink(
        downlinkPayload,
        downlinkLength,
        downlinkEvent
      );
    } else {
      DEBUG_PRINTF(
        "Unexpected downlink FPort: %u\n",
        downlinkEvent.fPort
      );
    }

  } else if (state > 0) {
    DEBUG_PRINTF(
      "Downlink received in RX%u, but no application payload was returned.\n",
      (unsigned int)state
    );

  } else if (state == 0) {
    DEBUG_PRINTLN(
      "Uplink transmitted; no application downlink received (normal)."
    );

  } else {
    printResult("Uplink exchange returned an error", state);
  }

  if (node.getFCntUp() != frameCountBefore) {
    DEBUG_PRINTLN(
      "Radio transmitted a frame. Check TTN to confirm delivery."
    );
  }

  DEBUG_PRINTF(
    "Next attempt in %lu minutes, subject to airtime limits.\n",
    (unsigned long)uplinkIntervalMinutes
  );

  waitForNextUplink(uplinkIntervalSeconds() * 1000UL);
}
