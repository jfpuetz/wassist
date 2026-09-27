#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/microphone/microphone.h"
#include "esphome/components/speaker/speaker.h"

#include <atomic>

namespace esphome::echo_test {

/// Records `duration` of audio from a microphone into PSRAM (16 bit mono)
/// and plays it back on a speaker afterwards. Meant as a hardware test for
/// microphone, amplifier and speaker without Home Assistant in the loop.
class EchoTest : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_microphone(microphone::Microphone *mic) { this->mic_ = mic; }
  void set_speaker(speaker::Speaker *spk) { this->spk_ = spk; }
  void set_duration_ms(uint32_t ms) { this->duration_ms_ = ms; }
  void set_gain_factor(int gain) { this->gain_factor_ = gain; }
  void set_normalize(bool n) { this->normalize_ = n; }
  void set_max_normalize_gain(float g) { this->max_normalize_gain_ = g; }

  /// Start recording; playback follows automatically.
  void start();
  bool is_busy() const { return this->state_.load() != STATE_IDLE; }

  Trigger<> *get_record_start_trigger() { return &this->record_start_trigger_; }
  Trigger<> *get_playback_start_trigger() { return &this->playback_start_trigger_; }
  Trigger<> *get_finished_trigger() { return &this->finished_trigger_; }

 protected:
  enum State : uint8_t { STATE_IDLE, STATE_RECORDING, STATE_PLAYING, STATE_DRAINING };

  void on_mic_data_(const std::vector<uint8_t> &data);
  void set_state_(State s);
  void finish_();
  /// Logs peak/RMS level and noise floor, optionally normalizes the recording in place.
  void analyze_and_normalize_(size_t samples);

  microphone::Microphone *mic_{nullptr};
  speaker::Speaker *spk_{nullptr};
  uint32_t duration_ms_{5000};
  int gain_factor_{4};
  bool normalize_{true};
  float max_normalize_gain_{16.0f};

  int16_t *buffer_{nullptr};
  size_t capacity_samples_{0};
  std::atomic<size_t> recorded_samples_{0};
  size_t played_bytes_{0};

  std::atomic<State> state_{STATE_IDLE};
  uint32_t state_since_{0};
  bool we_started_mic_{false};

  Trigger<> record_start_trigger_;
  Trigger<> playback_start_trigger_;
  Trigger<> finished_trigger_;
};

template<typename... Ts> class StartAction : public Action<Ts...>, public Parented<EchoTest> {
 public:
  void play(const Ts &...x) override { this->parent_->start(); }
};

}  // namespace esphome::echo_test
