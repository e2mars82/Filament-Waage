#pragma once

#include <stdint.h>

// Reine Zeitsteuerung ohne Arduino-/Hardwarezugriffe; dadurch separat testbar.
// Alle Zeitdifferenzen bleiben auch beim Überlauf von millis() korrekt.
class SensorTiming {
 public:
  constexpr SensorTiming(uint32_t pollMs, uint32_t settleMs, uint32_t sampleMs,
                         uint32_t timeoutMs, uint8_t samples)
      : pollMs_(pollMs), settleMs_(settleMs), sampleMs_(sampleMs),
        timeoutMs_(timeoutMs), samples_(samples) {}

  constexpr bool pollDue(uint32_t now) const {
    return !measuring_ && (!polled_ || uint32_t(now - pollAt_) >= pollMs_);
  }

  constexpr void pollStarted(uint32_t now) {
    polled_ = true;
    pollAt_ = now;
  }

  // Auch ein NFC-Schreibvorgang verwirft eine angefangene Messreihe und
  // startet die vollständige Beruhigungszeit erneut.
  constexpr void startMeasurements(uint32_t now) {
    measuring_ = true;
    referenceAt_ = now;
    count_ = 0;
    sum_ = 0;
  }

  constexpr bool settling(uint32_t now) const {
    return measuring_ && count_ == 0 && uint32_t(now - referenceAt_) < settleMs_;
  }

  constexpr bool sampleDue(uint32_t now) const {
    return measuring_ && uint32_t(now - referenceAt_) >= delayMs();
  }

  constexpr bool timedOut(uint32_t now) const {
    return measuring_ && uint32_t(now - referenceAt_) >= delayMs() + timeoutMs_;
  }

  constexpr void recordSample(uint32_t now, float value) {
    sum_ += value;
    ++count_;
    referenceAt_ = now;
    measuring_ = count_ < samples_;
  }

  constexpr void cancelMeasurements() { measuring_ = false; }
  constexpr bool measuring() const { return measuring_; }
  constexpr uint8_t count() const { return count_; }
  constexpr float mean() const { return count_ ? sum_ / count_ : 0.0F; }

 private:
  constexpr uint32_t delayMs() const { return count_ == 0 ? settleMs_ : sampleMs_; }

  uint32_t pollMs_, settleMs_, sampleMs_, timeoutMs_;
  uint8_t samples_;
  uint32_t pollAt_ = 0, referenceAt_ = 0;
  uint8_t count_ = 0;
  float sum_ = 0;
  bool polled_ = false, measuring_ = false;
};
