function decodeUplink(input) {
  const bytes = input.bytes;

  if (!Array.isArray(bytes) || bytes.length !== 4) {
    return {
      errors: ["Expected 4 payload bytes: battery voltage (2) and interval (2)."]
    };
  }

  // Battery voltage in 0.01 V, unsigned big-endian.
  const rawVoltage = (bytes[0] << 8) | bytes[1];

  // Uplink interval in whole minutes, unsigned big-endian.
  const intervalMinutes = (bytes[2] << 8) | bytes[3];

  return {
    data: {
      battery_voltage_v: rawVoltage / 100,
      uplink_interval_minutes: intervalMinutes
    }
  };
}
