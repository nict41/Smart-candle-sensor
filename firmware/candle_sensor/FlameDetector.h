// FlameDetector.h - platform-independent candle flame detection.
//
// Decides "candle lit / not lit" from up to three pieces of evidence:
//   1. Optical level   - near-IR brightness above the calibrated dark baseline.
//   2. Optical flicker - relative fluctuation in the 0.3-15 Hz band. Candle
//                        flames flicker here; sunlight and mains lighting
//                        (100/120 Hz) do not.
//   3. Thermal delta   - (optional) IR thermometer object temp minus ambient.
//
// Each evidence has its own on/off hysteresis. The candidate state is ON when
// enough evidences agree, and the reported state only changes after the
// candidate has been stable for onHoldMs / offHoldMs.
//
// No Arduino dependencies, so it can be unit tested on a desktop
// (see firmware/test).

#pragma once

#include <math.h>
#include <stdint.h>

struct FlameDetectorConfig {
  float sampleRateHz = 200.0f;

  // Filter corners.
  float lowPassHz = 15.0f;   // upper edge of flicker band
  float dcHz = 0.3f;         // lower edge of flicker band / DC tracker
  float rmsTauS = 2.0f;      // flicker energy averaging time

  // Optical level above baseline, in ADC counts.
  float levelOnCounts = 150.0f;
  float levelOffCounts = 80.0f;

  // Flicker modulation index (rms / level above baseline).
  float flickerOnRatio = 0.008f;
  float flickerOffRatio = 0.004f;
  float flickerMinRmsCounts = 1.5f;  // ignore flicker below the noise floor

  // Thermal delta (object - ambient), degrees C.
  float thermalOnDeltaC = 12.0f;
  float thermalOffDeltaC = 6.0f;

  // Debounce of the final state.
  uint32_t onHoldMs = 3000;
  uint32_t offHoldMs = 10000;

  // Level (above baseline) that maps to 100 % intensity.
  float fullScaleCounts = 2500.0f;
};

class FlameDetector {
 public:
  explicit FlameDetector(const FlameDetectorConfig &cfg = FlameDetectorConfig()) { configure(cfg); }

  void configure(const FlameDetectorConfig &cfg) {
    cfg_ = cfg;
    aLp_ = alpha(cfg_.lowPassHz);
    aDc_ = alpha(cfg_.dcHz);
    aVar_ = 1.0f - expf(-1.0f / (cfg_.rmsTauS * cfg_.sampleRateHz));
  }

  void setBaseline(float counts) { baseline_ = counts; }
  float baseline() const { return baseline_; }

  // Current smoothed optical level (use this to calibrate the baseline).
  float dcLevel() const { return dc_; }

  // Feed one raw ADC sample. Call at cfg.sampleRateHz.
  void addSample(float x) {
    if (!primed_) {
      lp1_ = lp2_ = dc_ = dc2_ = x;
      var_ = 0.0f;
      primed_ = true;
      return;
    }
    // Two cascaded low-pass stages: strong rejection of residual mains
    // flicker that aliases near Nyquist.
    lp1_ += aLp_ * (x - lp1_);
    lp2_ += aLp_ * (lp1_ - lp2_);
    // Double EMA (2*e1 - e2) tracks slow ramps (clouds, dimmers) without lag,
    // so they don't leak into the flicker band like a plain EMA would.
    dc_ += aDc_ * (x - dc_);
    dc2_ += aDc_ * (dc_ - dc2_);
    float ac = lp2_ - (2.0f * dc_ - dc2_);
    var_ += aVar_ * (ac * ac - var_);
  }

  // Feed a thermal reading (optional). Call whenever a new reading arrives.
  void addThermal(float objectC, float ambientC) {
    thermalDeltaC_ = objectC - ambientC;
    hasThermal_ = true;
  }

  // Update evidences and debounced state. Returns true if the state changed.
  bool evaluate(uint32_t nowMs) {
    float lvl = level();
    float rms = flickerRms();
    float ratio = flickerRatio();

    levelOk_ = hyst(levelOk_, lvl, cfg_.levelOnCounts, cfg_.levelOffCounts);
    bool flickerNow = hyst(flickerOk_, ratio, cfg_.flickerOnRatio, cfg_.flickerOffRatio);
    flickerOk_ = flickerNow && rms >= cfg_.flickerMinRmsCounts;
    if (hasThermal_) {
      thermalOk_ = hyst(thermalOk_, thermalDeltaC_, cfg_.thermalOnDeltaC, cfg_.thermalOffDeltaC);
    }

    bool candidate;
    if (hasThermal_) {
      // Any two of three.
      int votes = (levelOk_ ? 1 : 0) + (flickerOk_ ? 1 : 0) + (thermalOk_ ? 1 : 0);
      candidate = votes >= 2;
    } else {
      // Optical only: must be bright AND flickering (rejects sunlight/lamps).
      candidate = levelOk_ && flickerOk_;
    }

    if (candidate != candidate_) {
      candidate_ = candidate;
      candidateSinceMs_ = nowMs;
    }
    uint32_t hold = candidate_ ? cfg_.onHoldMs : cfg_.offHoldMs;
    if (candidate_ != lit_ && (uint32_t)(nowMs - candidateSinceMs_) >= hold) {
      lit_ = candidate_;
      return true;
    }
    return false;
  }

  bool lit() const { return lit_; }

  float level() const {
    float l = dc_ - baseline_;
    return l > 0.0f ? l : 0.0f;
  }
  float flickerRms() const { return sqrtf(var_ > 0.0f ? var_ : 0.0f); }
  float flickerRatio() const {
    float l = level();
    return l > 1.0f ? flickerRms() / l : 0.0f;
  }
  float thermalDeltaC() const { return thermalDeltaC_; }
  bool hasThermal() const { return hasThermal_; }

  float intensityPercent() const {
    float p = 100.0f * level() / cfg_.fullScaleCounts;
    return p < 0.0f ? 0.0f : (p > 100.0f ? 100.0f : p);
  }

  bool levelEvidence() const { return levelOk_; }
  bool flickerEvidence() const { return flickerOk_; }
  bool thermalEvidence() const { return thermalOk_; }

 private:
  float alpha(float fc) const { return 1.0f - expf(-2.0f * (float)M_PI * fc / cfg_.sampleRateHz); }

  static bool hyst(bool state, float v, float on, float off) { return state ? (v > off) : (v >= on); }

  FlameDetectorConfig cfg_;
  float aLp_ = 0, aDc_ = 0, aVar_ = 0;
  float lp1_ = 0, lp2_ = 0, dc_ = 0, dc2_ = 0, var_ = 0;
  bool primed_ = false;

  float baseline_ = 0.0f;
  float thermalDeltaC_ = 0.0f;
  bool hasThermal_ = false;

  bool levelOk_ = false, flickerOk_ = false, thermalOk_ = false;
  bool candidate_ = false, lit_ = false;
  uint32_t candidateSinceMs_ = 0;
};
