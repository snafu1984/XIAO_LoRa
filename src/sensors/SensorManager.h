#ifndef XIAO_LORA_SENSOR_MANAGER_H
#define XIAO_LORA_SENSOR_MANAGER_H

#include "Sensor.h"
#include "ina226/Ina226Sensor.h"

class SensorManager {
public:
  bool begin(TwoWire& wire);
  bool readBatteryVoltage(float& voltage);

private:
  Ina226Sensor ina226;
};

#endif
