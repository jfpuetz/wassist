#include "husb238.h"
#include "esphome/core/log.h"

namespace esphome::husb238 {

static const char *const TAG = "husb238";

// Index 0..5 -> 5, 9, 12, 15, 18, 20 V
static const float PDO_VOLTAGES[6] = {5.0f, 9.0f, 12.0f, 15.0f, 18.0f, 20.0f};
// Value for SRC_PDO[7:4] per index (note the gap: 15 V = 0b1000)
static const uint8_t PDO_SELECT[6] = {0b0001, 0b0010, 0b0011, 0b1000, 0b1001, 0b1010};
static const uint8_t MAX_REQUEST_ATTEMPTS = 3;

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
  // First evaluation shortly after boot instead of waiting a full update interval
  this->set_timeout("initial", 1000, [this]() { this->update(); });
}

void HUSB238Component::dump_config() {
  ESP_LOGCONFIG(TAG, "HUSB238 USB-PD sink:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication failed");
    return;
  }
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  Auto select: %s (max %.0f V)", YESNO(this->auto_select_), this->max_voltage_);
  LOG_SENSOR("  ", "Voltage", this->voltage_sensor_);
  LOG_SENSOR("  ", "Current", this->current_sensor_);
  LOG_SENSOR("  ", "Power", this->power_sensor_);

  uint8_t pdos[6];
  if (this->read_register(HUSB238_REG_SRC_PDO_5V, pdos, 6) == i2c::ERROR_OK) {
    for (int i = 0; i < 6; i++) {
      if (pdos[i] & 0x80)
        ESP_LOGCONFIG(TAG, "  Source offers %2.0f V @ %.2f A", PDO_VOLTAGES[i], decode_current_(pdos[i]));
    }
  }
}

bool HUSB238Component::read_contract_(float &voltage, float &current, bool &attached) {
  uint8_t status[2];
  if (this->read_register(HUSB238_REG_PD_STATUS0, status, 2) != i2c::ERROR_OK)
    return false;

  attached = status[1] & 0x40;
  const uint8_t vcode = status[0] >> 4;
  voltage = (vcode >= 1 && vcode <= 6) ? PDO_VOLTAGES[vcode - 1] : 0.0f;
  current = decode_current_(status[0]);

  if (voltage == 0.0f && attached) {
    // No PD contract: plain USB 5 V, current from Type-C advertisement
    static const float CURRENT_5V[4] = {0.5f, 1.5f, 2.4f, 3.0f};
    voltage = 5.0f;
    current = CURRENT_5V[status[1] & 0x03];
  }
  if (!attached) {
    voltage = 0.0f;
    current = 0.0f;
  }
  return true;
}

int HUSB238Component::best_pdo_(float &best_voltage, float &best_current) {
  uint8_t pdos[6];
  if (this->read_register(HUSB238_REG_SRC_PDO_5V, pdos, 6) != i2c::ERROR_OK)
    return -1;

  int best = -1;
  float best_power = 0.0f;
  for (int i = 0; i < 6; i++) {
    if (!(pdos[i] & 0x80) || PDO_VOLTAGES[i] > this->max_voltage_ + 0.01f)
      continue;
    const float amps = decode_current_(pdos[i]);
    const float power = PDO_VOLTAGES[i] * amps;
    // Most power wins; on a tie prefer the lower voltage (less converter loss)
    if (power > best_power + 0.01f) {
      best = i;
      best_power = power;
      best_voltage = PDO_VOLTAGES[i];
      best_current = amps;
    }
  }
  return best;
}

void HUSB238Component::request_pdo_(int index) {
  const uint8_t select = PDO_SELECT[index] << 4;
  if (this->write_register(HUSB238_REG_SRC_PDO, &select, 1) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "Writing SRC_PDO failed");
    return;
  }
  const uint8_t cmd = HUSB238_CMD_REQUEST_PDO;
  if (this->write_register(HUSB238_REG_GO_COMMAND, &cmd, 1) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "Writing GO_COMMAND failed");
    return;
  }
  this->request_attempts_++;
  ESP_LOGI(TAG, "Requested %.0f V (attempt %u)", PDO_VOLTAGES[index], this->request_attempts_);
  // Re-check the contract once the negotiation had time to finish
  this->set_timeout("verify", 500, [this]() { this->update(); });
}

void HUSB238Component::update() {
  float voltage, current;
  bool attached;
  if (!this->read_contract_(voltage, current, attached)) {
    ESP_LOGW(TAG, "Reading PD status failed");
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();

  if (this->auto_select_ && attached) {
    float want_v = 0, want_a = 0;
    const int best = this->best_pdo_(want_v, want_a);
    const bool better = best >= 0 && (want_v * want_a) > (voltage * current) + 0.01f;
    if (better && this->request_attempts_ < MAX_REQUEST_ATTEMPTS) {
      ESP_LOGI(TAG, "Contract %.0f V / %.2f A, source offers %.0f V / %.2f A", voltage, current, want_v, want_a);
      this->request_pdo_(best);
    } else if (better && this->request_attempts_ == MAX_REQUEST_ATTEMPTS) {
      ESP_LOGW(TAG, "Source did not accept %.0f V after %u attempts, staying at %.0f V", want_v,
               MAX_REQUEST_ATTEMPTS, voltage);
      this->request_attempts_++;  // log once
    }
  }

  ESP_LOGD(TAG, "Contract: %.0f V / %.2f A (attached=%s)", voltage, current, YESNO(attached));
  if (this->voltage_sensor_ != nullptr)
    this->voltage_sensor_->publish_state(voltage);
  if (this->current_sensor_ != nullptr)
    this->current_sensor_->publish_state(current);
  if (this->power_sensor_ != nullptr)
    this->power_sensor_->publish_state(voltage * current);
}

}  // namespace esphome::husb238
