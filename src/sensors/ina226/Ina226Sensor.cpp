#include "Ina226Sensor.h"

Ina226Sensor::Ina226Sensor(uint8_t address)
  : i2cAddress(address) {
}

bool Ina226Sensor::begin(TwoWire& bus) {
  wire = &bus;

  // Read the bus-voltage register to verify that the INA226 responds.
  uint16_t rawVoltage = 0;
  return readRegister(0x02, rawVoltage);
}

bool Ina226Sensor::readBatteryVoltage(float& voltage) {
  uint16_t rawVoltage = 0;

  if (!readRegister(0x02, rawVoltage)) {
    return false;
  }

  // INA226 bus-voltage register: 1.25 mV per bit.
  // No shunt calibration is required for bus-voltage measurement.
  voltage = static_cast<float>(rawVoltage) * 0.00125f;
  return true;
}

bool Ina226Sensor::readRegister(uint8_t reg, uint16_t& value) {
  if (wire == nullptr) {
    return false;
  }

  wire->beginTransmission(i2cAddress);
  wire->write(reg);

  if (wire->endTransmission(false) != 0) {
    return false;
  }

  if (wire->requestFrom(i2cAddress, static_cast<uint8_t>(2)) != 2) {
    return false;
  }

  uint8_t highByte = wire->read();
  uint8_t lowByte = wire->read();

  value = (static_cast<uint16_t>(highByte) << 8) | lowByte;
  return true;
}
