#include "SensorManager.h"

bool SensorManager::begin(TwoWire& wire) {
  return ina226.begin(wire);
}

bool SensorManager::readBatteryVoltage(float& voltage) {
  return ina226.readBatteryVoltage(voltage);
}
