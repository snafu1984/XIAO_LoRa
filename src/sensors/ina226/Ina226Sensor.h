#ifndef XIAO_LORA_INA226_SENSOR_H
#define XIAO_LORA_INA226_SENSOR_H

#include "../Sensor.h"

class Ina226Sensor : public Sensor {
public:
  explicit Ina226Sensor(uint8_t address = 0x40);

  bool begin(TwoWire& wire) override;
  bool readBatteryVoltage(float& voltage) override;

private:
  TwoWire* wire = nullptr;
  uint8_t i2cAddress;
  bool readRegister(uint8_t reg, uint16_t& value);
};

#endif
