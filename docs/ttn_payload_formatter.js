function decodeUplink(input) {
  const bytes = input.bytes;

  if (!Array.isArray(bytes) || bytes.length !== 4) {
    return {
      errors: ["Expected 4 payload bytes: temperature (2) and interval (2)."]
    };
  }

  // Signed temperature in hundredths of a degree Celsius, big-endian.
  const rawTemperature = (bytes[0] << 8) | bytes[1];
  const signedTemperature = rawTemperature & 0x8000
    ? rawTemperature - 0x10000
    : rawTemperature;

  // Uplink interval in whole minutes, unsigned big-endian.
  const intervalMinutes = (bytes[2] << 8) | bytes[3];

  return {
    data: {
      temperature_c: signedTemperature / 100,
      uplink_interval_minutes: intervalMinutes
    }
  };
}
