
#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <math.h>
#include "config.h"

void haltDevice(const char* message);

Preferences store;
uint32_t uplinkIntervalMinutes = defaultUplinkIntervalMinutes;

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
    Serial.println("Stored uplink interval is invalid; using default.");
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

  Serial.printf(
    "New uplink interval saved: %lu minutes.\n",
    (unsigned long)uplinkIntervalMinutes
  );
}

void processDownlink(
  const uint8_t* payload,
  size_t length,
  const LoRaWANEvent_t& event
) {
  if (event.fPort != commandFPort) {
    Serial.printf(
      "Downlink ignored: unexpected FPort %u.\n",
      event.fPort
    );
    return;
  }

  if (length != 3) {
    Serial.printf(
      "Downlink ignored: expected 3 bytes, received %u.\n",
      (unsigned int)length
    );
    return;
  }

  if (payload[0] != commandSetInterval) {
    Serial.printf(
      "Downlink ignored: unknown command 0x%02X.\n",
      payload[0]
    );
    return;
  }

  uint32_t minutes =
    ((uint32_t)payload[1] << 8) | payload[2];

  if (minutes < minUplinkIntervalMinutes ||
      minutes > maxUplinkIntervalMinutes) {
    Serial.printf(
      "Downlink ignored: interval %lu minutes is outside allowed range.\n",
      (unsigned long)minutes
    );
    return;
  }

  Serial.printf(
    "Interval command received: %lu minutes.\n",
    (unsigned long)minutes
  );

  saveUplinkInterval(minutes);
}

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

// Diagnostic wait without ESP32 Light Sleep.
// USB CDC remains active during the waiting period.
void waitWithoutSleep(uint32_t waitMs) {
  if (waitMs == 0) {
    return;
  }

  Serial.printf(
    "\nWaiting without Light Sleep for %lu ms...\n",
    (unsigned long)waitMs
  );

  Serial.flush();

  delay(waitMs);

  Serial.println("Wait finished.");
  Serial.flush();
}

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

  Serial.println("Join nonces saved to flash.");
}

void restoreNonces() {
  if (!store.isKey("nonces")) {
    Serial.println("No saved nonces: first run of this firmware.");
    return;
  }

  // Nonce counters belong to one device identity.
  if (store.getULong64("deveui", 0) != devEUI) {
    Serial.println(
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

  Serial.println("Saved join nonces restored.");
}

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
    Serial.printf(
      "\nJoining TTN using OTAA, attempt %u/%u...\n",
      (unsigned int)(attempt + 1),
      (unsigned int)maxAttempts
    );

    int16_t state = node.activateOTAA();

    saveNonces();

    printResult("Join result", state);

    if (state == RADIOLIB_LORAWAN_NEW_SESSION ||
        state == RADIOLIB_LORAWAN_SESSION_RESTORED ||
        (state == RADIOLIB_ERR_NONE && node.isActivated())) {
      Serial.println("Joined TTN!");

      Serial.printf(
        "DevAddr: %08lX\n",
        (unsigned long)node.getDevAddr()
      );

      return;
    }

    if (attempt + 1 < maxAttempts) {
      uint32_t retrySeconds = 300UL << attempt;

      if (retrySeconds > 3600UL) {
        retrySeconds = 3600UL;
      }

      Serial.printf(
        "No successful join. Retrying in %lu minutes.\n",
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

void setup() {
  Serial.begin(115200);

  // Allow USB serial to connect, but do not require a computer.
  unsigned long started = millis();

  while (!Serial && millis() - started < 10000UL) {
    delay(10);
  }

  Serial.println("\nXIAO chip-temperature LoRaWAN sensor");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);

  Serial.println("Initializing SX1262...");

  int16_t state = radio.begin(868.1);

  if (state != RADIOLIB_ERR_NONE) {
    printResult("Radio initialization failed", state);
    haltDevice("Check the Wio module connection and radio pins.");
  }

  Serial.println("Radio initialization OK");

  radio.setRfSwitchPins(LORA_RF_SW, RADIOLIB_NC);

  Serial.println("RF switch configured (GPIO38).");

  if (!store.begin(STORAGE_NAMESPACE, false)) {
    haltDevice("ERROR: Could not open ESP32 persistent storage.");
  }

  loadUplinkInterval();

  Serial.printf(
    "Configured uplink interval: %lu minutes.\n",
    (unsigned long)uplinkIntervalMinutes
  );

  node.setADR(true);
  node.setDutyCycle(true, 1250);
  node.setDeviceStatus(0);

  joinNetwork();

  Serial.println("Ready to send chip temperature.");
}

void loop() {
  uint32_t waitMs = node.timeUntilUplink();

  if (waitMs > 0) {
    Serial.printf(
      "Airtime wait: %lu seconds\n",
      (unsigned long)((waitMs + 999UL) / 1000UL)
    );

    waitWithoutSleep(waitMs);
  }

  float temperature = temperatureRead();

  if (!isfinite(temperature) ||
      temperature < -327.68f ||
      temperature > 327.67f) {
    Serial.println("Invalid temperature: skipping this reading.");

    waitWithoutSleep(uplinkIntervalSeconds() * 1000UL);
    return;
  }

  // Uplink payload, big-endian:
  // bytes 0..1: signed temperature in 0.01 C
  // bytes 2..3: configured interval in whole minutes

  int16_t temperature100 =
    (int16_t)lroundf(temperature * 100.0f);

  uint16_t encodedTemperature = (uint16_t)temperature100;
  uint16_t encodedInterval = (uint16_t)uplinkIntervalMinutes;

  uint8_t payload[4] = {
    (uint8_t)(encodedTemperature >> 8),
    (uint8_t)(encodedTemperature & 0xFF),
    (uint8_t)(encodedInterval >> 8),
    (uint8_t)(encodedInterval & 0xFF)
  };

  Serial.printf(
    "\nSending chip temperature: %.2f C\n",
    temperature
  );

  Serial.printf(
    "Configured interval: %lu minutes\n",
    (unsigned long)uplinkIntervalMinutes
  );

  Serial.printf(
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

  // Unconfirmed uplink, followed by RX1 and RX2.
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

  // Diagnostic output.
  Serial.printf("sendReceive() state: %d\n", state);

  Serial.printf(
    "downlinkLength: %u\n",
    (unsigned int)downlinkLength
  );

  Serial.printf(
    "downlinkEvent FPort: %u\n",
    downlinkEvent.fPort
  );

  Serial.printf(
    "downlinkEvent FCnt: %lu\n",
    (unsigned long)downlinkEvent.fCnt
  );

  if (downlinkLength > 0) {
    Serial.print("Downlink application payload: ");

    for (size_t i = 0; i < downlinkLength; i++) {
      Serial.printf("%02X ", downlinkPayload[i]);
    }

    Serial.println();
  }

  saveNonces();

  // IMPORTANT:
  // Check the application payload length first.
  // A valid downlink can be present even if state == 0.
  if (downlinkLength > 0) {
    Serial.println("Application downlink received!");

    if (downlinkEvent.fPort == commandFPort) {
      processDownlink(
        downlinkPayload,
        downlinkLength,
        downlinkEvent
      );
    } else {
      Serial.printf(
        "Unexpected downlink FPort: %u\n",
        downlinkEvent.fPort
      );
    }

  } else if (state > 0) {
    Serial.printf(
      "Downlink received in RX%u, but no application payload was returned.\n",
      (unsigned int)state
    );

  } else if (state == 0) {
    Serial.println(
      "Uplink transmitted; no application downlink received (normal)."
    );

  } else {
    printResult("Uplink exchange returned an error", state);
  }

  if (node.getFCntUp() != frameCountBefore) {
    Serial.println(
      "Radio transmitted a frame. Check TTN to confirm delivery."
    );
  }

  Serial.printf(
    "Next attempt in %lu minutes, subject to airtime limits.\n",
    (unsigned long)uplinkIntervalMinutes
  );

  waitWithoutSleep(uplinkIntervalSeconds() * 1000UL);
}
