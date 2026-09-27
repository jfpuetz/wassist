#include "echo_test.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace esphome::echo_test {

static const char *const TAG = "echo_test";

static const uint32_t SAMPLE_RATE = 16000;
static const size_t PLAY_CHUNK_BYTES = 2048;
static const uint32_t RECORD_GRACE_MS = 1500;   // extra time before giving up on missing mic data
static const uint32_t PLAYBACK_TIMEOUT_MS = 5000;  // on top of the recording length

void EchoTest::setup() {
  this->capacity_samples_ = (size_t) SAMPLE_RATE * this->duration_ms_ / 1000;
  RAMAllocator<int16_t> allocator(RAMAllocator<int16_t>::ALLOC_EXTERNAL);
  this->buffer_ = allocator.allocate(this->capacity_samples_);
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate %u bytes in PSRAM", (unsigned) (this->capacity_samples_ * sizeof(int16_t)));
    this->mark_failed();
    return;
  }
  this->mic_->add_data_callback([this](const std::vector<uint8_t> &data) { this->on_mic_data_(data); });
}

void EchoTest::dump_config() {
  ESP_LOGCONFIG(TAG, "Echo test:");
  ESP_LOGCONFIG(TAG, "  Duration: %u ms (%u samples @ 16 kHz)", (unsigned) this->duration_ms_,
                (unsigned) this->capacity_samples_);
  ESP_LOGCONFIG(TAG, "  Gain factor: %d", this->gain_factor_);
  ESP_LOGCONFIG(TAG, "  Normalize: %s (max x%.0f)", YESNO(this->normalize_), this->max_normalize_gain_);
}

void EchoTest::set_state_(State s) {
  this->state_.store(s);
  this->state_since_ = millis();
}

void EchoTest::start() {
  if (this->is_failed())
    return;
  if (this->is_busy()) {
    ESP_LOGW(TAG, "Already running");
    return;
  }
  this->recorded_samples_.store(0);
  this->played_bytes_ = 0;
  this->we_started_mic_ = !this->mic_->is_running();
  this->set_state_(STATE_RECORDING);
  this->mic_->start();
  ESP_LOGI(TAG, "Recording %u ms ...", (unsigned) this->duration_ms_);
  this->record_start_trigger_.trigger();
}

// Runs in the microphone task.
void EchoTest::on_mic_data_(const std::vector<uint8_t> &data) {
  if (this->state_.load() != STATE_RECORDING)
    return;

  const auto info = this->mic_->get_audio_stream_info();
  const uint8_t bits = info.get_bits_per_sample();
  const uint8_t channels = std::max<uint8_t>(1, info.get_channels());
  const size_t bytes_per_sample = bits / 8;
  if (bytes_per_sample != 2 && bytes_per_sample != 4)
    return;
  const size_t frame_bytes = bytes_per_sample * channels;

  size_t pos = this->recorded_samples_.load();
  for (size_t off = 0; off + frame_bytes <= data.size() && pos < this->capacity_samples_; off += frame_bytes) {
    int32_t sample;
    if (bytes_per_sample == 4) {
      int32_t raw;
      std::memcpy(&raw, &data[off], 4);  // first channel only
      sample = raw >> 16;
    } else {
      int16_t raw;
      std::memcpy(&raw, &data[off], 2);
      sample = raw;
    }
    sample *= this->gain_factor_;
    sample = std::clamp<int32_t>(sample, INT16_MIN, INT16_MAX);
    this->buffer_[pos++] = (int16_t) sample;
  }
  this->recorded_samples_.store(pos);
}

static float to_dbfs(float v) { return v <= 0.0f ? -120.0f : 20.0f * log10f(v / 32768.0f); }

void EchoTest::analyze_and_normalize_(size_t samples) {
  // Overall peak / RMS and the quietest 100 ms window as noise-floor estimate
  const size_t window = SAMPLE_RATE / 10;
  int32_t peak = 0;
  double sum_sq = 0.0;
  double win_sq = 0.0;
  double min_win_rms = 1e9;
  for (size_t i = 0; i < samples; i++) {
    const int32_t s = this->buffer_[i];
    peak = std::max<int32_t>(peak, std::abs(s));
    const double sq = (double) s * s;
    sum_sq += sq;
    win_sq += sq;
    if ((i + 1) % window == 0) {
      min_win_rms = std::min(min_win_rms, std::sqrt(win_sq / window));
      win_sq = 0.0;
    }
  }
  const float rms = samples ? (float) std::sqrt(sum_sq / samples) : 0.0f;
  const float noise = min_win_rms < 1e9 ? (float) min_win_rms : rms;
  ESP_LOGI(TAG, "Level (after gain_factor %d): peak %.1f dBFS, RMS %.1f dBFS, noise floor %.1f dBFS, SNR ~%.0f dB",
           this->gain_factor_, to_dbfs(peak), to_dbfs(rms), to_dbfs(noise),
           to_dbfs(rms) - to_dbfs(noise));

  if (!this->normalize_ || peak == 0)
    return;
  const float target = 0.7f * 32767.0f;  // ~ -3 dBFS
  const float gain = std::min(this->max_normalize_gain_, target / (float) peak);
  if (gain <= 1.05f)
    return;
  for (size_t i = 0; i < samples; i++) {
    const int32_t v = (int32_t) lrintf(this->buffer_[i] * gain);
    this->buffer_[i] = (int16_t) std::clamp<int32_t>(v, INT16_MIN, INT16_MAX);
  }
  ESP_LOGI(TAG, "Normalized playback by x%.1f (+%.1f dB)", gain, 20.0f * log10f(gain));
}

void EchoTest::finish_() {
  this->set_state_(STATE_IDLE);
  ESP_LOGI(TAG, "Echo test finished");
  this->finished_trigger_.trigger();
}

void EchoTest::loop() {
  const State state = this->state_.load();
  const uint32_t elapsed = millis() - this->state_since_;

  switch (state) {
    case STATE_IDLE:
      return;

    case STATE_RECORDING: {
      // Someone else (e.g. micro_wake_word stopping) may have stopped the mic – restart it.
      if (this->mic_->is_stopped())
        this->mic_->start();

      const size_t got = this->recorded_samples_.load();
      const bool full = got >= this->capacity_samples_;
      const bool timeout = elapsed > this->duration_ms_ + RECORD_GRACE_MS;
      if (!full && !timeout)
        return;
      if (this->we_started_mic_)
        this->mic_->stop();
      if (got == 0) {
        ESP_LOGE(TAG, "No audio received from microphone");
        this->finish_();
        return;
      }
      ESP_LOGI(TAG, "Recorded %u samples (%.1f s), playing back", (unsigned) got, got / (float) SAMPLE_RATE);
      this->analyze_and_normalize_(got);
      this->spk_->set_audio_stream_info(audio::AudioStreamInfo(16, 1, SAMPLE_RATE));
      this->spk_->start();
      this->set_state_(STATE_PLAYING);
      this->playback_start_trigger_.trigger();
      return;
    }

    case STATE_PLAYING: {
      const size_t total_bytes = this->recorded_samples_.load() * sizeof(int16_t);
      if (this->played_bytes_ < total_bytes) {
        const size_t len = std::min(PLAY_CHUNK_BYTES, total_bytes - this->played_bytes_);
        const size_t written =
            this->spk_->play(reinterpret_cast<const uint8_t *>(this->buffer_) + this->played_bytes_, len, 0);
        this->played_bytes_ += written;
      } else {
        this->spk_->finish();
        this->set_state_(STATE_DRAINING);
      }
      if (elapsed > this->duration_ms_ + PLAYBACK_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Playback timeout");
        this->spk_->stop();
        this->finish_();
      }
      return;
    }

    case STATE_DRAINING:
      if (this->spk_->is_stopped() || elapsed > PLAYBACK_TIMEOUT_MS) {
        if (!this->spk_->is_stopped())
          this->spk_->stop();
        this->finish_();
      }
      return;
  }
}

}  // namespace esphome::echo_test
