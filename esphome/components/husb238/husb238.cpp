#include "husb238.h"
#include "esphome/core/log.h"

namespace esphome::husb238 {

static const char *const TAG = "husb238";

static const float PDO_VOLTAGES[] = {5.0f, 9.0f, 12.0f, 15.0f, 18.0f, 20.0f};

float HUSB238Component::decode_voltage_(uint8_t code) {
  // 0 = unattached / no PD contract, 1..6 = 5, 9, 12, 15, 18, 20 V
  if (code >= 1 && code <= 6)
    return PDO_VOLTAGES[code - 1];
  return 0.0f;
}

float HUSB238Component::decode_current_(uint8_t code) {
  static const float CURRENTS[16] = {0.50f, 0.70f, 1.00f, 1.25f, 1.50f, 1.75f, 2.00f, 2.25f,
                                     2.50f, 2.75f, 3.00f, 3.25f, 3.50f, 4.00f, 4.50f, 5.00f};
  return CURRENTS[code & 0x0F];
}

void HUSB238Component::setup() {
  uint8_t status1;
  if (this->read_register(HUSB238_REG_PD_STATUS1, &status1, 1) != i2c::ERROR_OK) {
    ESP_LOGE(TAG, "HUSB238 not responding");
    this->mark_failed();
    return;
  }
}

void HUSB238Component::dump_config() {
  ESP_LOGCONFIG(TAG, "HUSB238 USB-PD sink:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication failed");
    return;
  }
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Voltage", this->voltage_sensor_);
  LOG_SENSOR("  ", "Current", this->current_sensor_);
  this->log_source_pdos_();
}

void HUSB238Component::log_source_pdos_() {
  uint8_t pdos[6];
  if (this->read_register(HUSB238_REG_SRC_PDO_5V, pdos, 6) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "  Could not read source PDOs");
    return;
  }
  for (int i = 0; i < 6; i++) {
    if (pdos[i] & 0x80) {
      ESP_LOGCONFIG(TAG, "  Source offers %2.0f V @ %.2f A", PDO_VOLTAGES[i], decode_current_(pdos[i] & 0x0F));
    }
  }
}

void HUSB238Component::update() {
  uint8_t status[2];
  if (this->read_register(HUSB238_REG_PD_STATUS0, status, 2) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "Reading PD status failed");
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();

  const bool attached = status[1] & 0x40;
  float voltage = decode_voltage_(status[0] >> 4);
  float current = decode_current_(status[0] & 0x0F);

  if (voltage == 0.0f && attached && (status[1] & 0x04)) {
    // No PD contract, plain USB 5 V
    voltage = 5.0f;
    static const float CURRENT_5V[4] = {0.5f, 1.5f, 2.4f, 3.0f};  // default, 1.5 A, 2.4 A, 3 A
    current = CURRENT_5V[status[1] & 0x03];
  }
  if (!attached) {
    voltage = 0.0f;
    current = 0.0f;
  }

  ESP_LOGD(TAG, "Contract: %.0f V / %.2f A (attached=%s)", voltage, current, YESNO(attached));
  if (this->voltage_sensor_ != nullptr)
    this->voltage_sensor_->publish_state(voltage);
  if (this->current_sensor_ != nullptr)
    this->current_sensor_->publish_state(current);
}

}  // namespace esphome::husb238
