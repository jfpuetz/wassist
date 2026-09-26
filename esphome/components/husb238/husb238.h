#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome::husb238 {

// Register map (Hynetek HUSB238 datasheet / Adafruit_HUSB238)
static const uint8_t HUSB238_REG_PD_STATUS0 = 0x00;  // [7:4] voltage code, [3:0] current code
static const uint8_t HUSB238_REG_PD_STATUS1 = 0x01;  // [7] CC dir, [6] attached, [5:3] PD response, [2] 5V contract
static const uint8_t HUSB238_REG_SRC_PDO_5V = 0x02;  // 0x02..0x07 = 5/9/12/15/18/20 V, [7] detected, [3:0] current
static const uint8_t HUSB238_REG_SRC_PDO = 0x08;     // [7:4] PDO to request
static const uint8_t HUSB238_REG_GO_COMMAND = 0x09;  // [4:0] command

static const uint8_t HUSB238_CMD_REQUEST_PDO = 0b00001;
static const uint8_t HUSB238_CMD_GET_SRC_CAP = 0b00100;

/// HUSB238 USB-PD sink.
/// Reports the negotiated contract and, with auto_select, requests the source PDO
/// that offers the most power without exceeding max_voltage.
/// On power-up the breakout uses its jumper setting until the first I2C request.
class HUSB238Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_voltage_sensor(sensor::Sensor *s) { voltage_sensor_ = s; }
  void set_current_sensor(sensor::Sensor *s) { current_sensor_ = s; }
  void set_power_sensor(sensor::Sensor *s) { power_sensor_ = s; }
  void set_max_voltage(float v) { max_voltage_ = v; }
  void set_auto_select(bool a) { auto_select_ = a; }

 protected:
  static float decode_current_(uint8_t code);
  bool read_contract_(float &voltage, float &current, bool &attached);
  /// Returns index 0..5 of the best PDO (5..20 V), or -1 if none readable.
  int best_pdo_(float &best_voltage, float &best_current);
  void request_pdo_(int index);

  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  sensor::Sensor *power_sensor_{nullptr};
  float max_voltage_{20.0f};
  bool auto_select_{true};
  uint8_t request_attempts_{0};
};

}  // namespace esphome::husb238
