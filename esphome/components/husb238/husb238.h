#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome::husb238 {

// Register map (Hynetek HUSB238 datasheet / Adafruit_HUSB238)
static const uint8_t HUSB238_REG_PD_STATUS0 = 0x00;  // [7:4] voltage code, [3:0] current code
static const uint8_t HUSB238_REG_PD_STATUS1 = 0x01;  // [7] CC dir, [6] attached, [5:3] PD response, [2] 5V contract
static const uint8_t HUSB238_REG_SRC_PDO_5V = 0x02;  // 0x02..0x07 = 5/9/12/15/18/20 V, [7] detected, [3:0] current

/// Read-only status of the HUSB238 USB-PD sink.
/// The requested voltage is set by jumper on the breakout; this component
/// only reports the negotiated contract and the source's advertised PDOs.
class HUSB238Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_voltage_sensor(sensor::Sensor *s) { voltage_sensor_ = s; }
  void set_current_sensor(sensor::Sensor *s) { current_sensor_ = s; }

 protected:
  static float decode_voltage_(uint8_t code);
  static float decode_current_(uint8_t code);
  void log_source_pdos_();

  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
};

}  // namespace esphome::husb238
