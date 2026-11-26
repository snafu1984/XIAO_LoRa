#ifndef XIAO_LORA_SENSOR_H
#define XIAO_LORA_SENSOR_H

#include <Arduino.h>
#include <Wire.h>

class Sensor {
public:
  virtual ~Sensor() = default;
  virtual bool begin(TwoWire& wire) = 0;
  virtual bool readBatteryVoltage(float& voltage) = 0;
};

#endif
