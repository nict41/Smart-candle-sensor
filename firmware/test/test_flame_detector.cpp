// Host-side tests for FlameDetector using synthetic signals.
// Build & run:  make -C firmware/test

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>

#include "../candle_sensor/FlameDetector.h"

static int failures = 0;

#define CHECK(cond, msg)                               \
  do {                                                 \
    if (!(cond)) {                                     \
      std::printf("  FAIL: %s (%s)\n", msg, #cond);    \
      failures++;                                      \
    } else {                                           \
      std::printf("  ok:   %s\n", msg);                \
    }                                                  \
  } while (0)

static const float FS = 200.0f;
static const float PI = 3.14159265f;
static const float DARK = 60.0f;

// Runs `seconds` of signal through the detector, evaluating every 100 ms like
// the firmware. Returns the time (s) of the first state change, or -1.
struct Run {
  FlameDetector &det;
  float t = 0.0f;
  std::mt19937 rng{1234};

  float run(float seconds, const std::function<float(float)> &signal,
            const std::function<void(float)> &thermal = nullptr) {
    std::normal_distribution<float> noise(0.0f, 1.0f);
    float firstChange = -1.0f;
    int n = (int)(seconds * FS);
    for (int i = 0; i < n; i++) {
      det.addSample(signal(t) + noise(rng));
      if (i % 20 == 0) {
        if (thermal) thermal(t);
        if (det.evaluate((uint32_t)(t * 1000.0f)) && firstChange < 0) firstChange = t;
      }
      t += 1.0f / FS;
    }
    return firstChange;
  }
};

// Candle: steady-ish level with multi-tone flicker of ~3 % plus random wander.
static float candle(float t, float level) {
  float f = 0.018f * sinf(2 * PI * 2.7f * t) + 0.012f * sinf(2 * PI * 7.9f * t + 1.0f) +
            0.008f * sinf(2 * PI * 11.3f * t + 2.0f);
  return DARK + level * (1.0f + f);
}

static void testDark() {
  std::printf("dark room\n");
  FlameDetector det;
  det.setBaseline(DARK);
  Run r{det};
  r.run(30, [](float) { return DARK; });
  CHECK(!det.lit(), "stays off in the dark");
}

static void testCandleOnOff() {
  std::printf("candle lit then blown out\n");
  FlameDetector det;
  det.setBaseline(DARK);
  Run r{det};
  r.run(5, [](float) { return DARK; });
  float on = r.run(20, [](float t) { return candle(t, 800.0f); });
  CHECK(det.lit(), "turns on with a flickering flame");
  CHECK(on > 0 && on - 5.0f < 8.0f, "turns on within 8 s of lighting");
  std::printf("        on after %.1f s, flicker ratio %.4f\n", on - 5.0f, det.flickerRatio());

  float off = r.run(30, [](float) { return DARK; });
  CHECK(!det.lit(), "turns off after extinguishing");
  CHECK(off > 0 && off - 25.0f >= 10.0f && off - 25.0f < 16.0f, "off-hold of ~10 s respected");
  std::printf("        off after %.1f s\n", off - 25.0f);
}

static void testSunlight() {
  std::printf("steady sunlight (bright, no flicker)\n");
  FlameDetector det;
  det.setBaseline(DARK);
  Run r{det};
  // Bright plus a very slow cloud drift.
  r.run(60, [](float t) { return DARK + 1500.0f + 200.0f * sinf(2 * PI * 0.02f * t); });
  CHECK(!det.lit(), "sunlight alone is not a flame");
  std::printf("        flicker ratio %.4f\n", det.flickerRatio());
}

static void testMainsLamp() {
  std::printf("incandescent lamp, 100 Hz ripple residue after RC filter\n");
  FlameDetector det;
  det.setBaseline(DARK);
  Run r{det};
  // 5 % ripple left after the hardware anti-alias filter, sampled at 200 Hz
  // with a drifting phase, so it lands near Nyquist.
  r.run(60, [](float t) { return DARK + 600.0f * (1.0f + 0.05f * sinf(2 * PI * 100.3f * t)); });
  CHECK(!det.lit(), "mains-flicker lamp is not a flame");
  std::printf("        flicker ratio %.4f\n", det.flickerRatio());
}

static void testThermalJarCandle() {
  std::printf("jar candle: moderate light, weak flicker, warm glass\n");
  FlameDetector det;
  det.setBaseline(DARK);
  Run r{det};
  // Glass diffuses the flame: modest level, flicker below threshold.
  r.run(30, [](float t) { return DARK + 300.0f * (1.0f + 0.002f * sinf(2 * PI * 5 * t)); },
        [&](float t) { det.addThermal(22.0f + (t < 5 ? 0 : 20.0f), 22.0f); });
  CHECK(det.lit(), "level + thermal votes turn it on");
}

static void testThermalRejectsSun() {
  std::printf("sunlight with IR thermometer seeing a cool target\n");
  FlameDetector det;
  det.setBaseline(DARK);
  Run r{det};
  r.run(60, [](float) { return DARK + 1500.0f; }, [&](float) { det.addThermal(24.0f, 22.0f); });
  CHECK(!det.lit(), "one vote out of three is not enough");
}

static void testIntensity() {
  std::printf("intensity scaling\n");
  FlameDetector det;
  det.setBaseline(DARK);
  Run r{det};
  r.run(20, [](float) { return DARK + 1250.0f; });
  float p = det.intensityPercent();
  CHECK(p > 45.0f && p < 55.0f, "half of full scale reads ~50 %");
}

int main() {
  testDark();
  testCandleOnOff();
  testSunlight();
  testMainsLamp();
  testThermalJarCandle();
  testThermalRejectsSun();
  testIntensity();
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
