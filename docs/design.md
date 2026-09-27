# Design: Zigbee Candle Flame Sensor

## 1. Goal

A small, USB-powered Zigbee device that sits near a candle and reports to the
smart home whether the candle is **lit** or **out**, so automations can warn
about candles left burning (nobody home, bedtime, burning too long).

**Requirements**

| # | Requirement | Target |
|---|---|---|
| R1 | Detect lit → report | < 10 s |
| R2 | Detect out → report | < 20 s |
| R3 | No false "lit" from daylight, lamps, TV | Required |
| R4 | Works with open candles and jar candles | Required |
| R5 | Non-contact, safe distance from flame | ≥ 20 cm |
| R6 | Standard Zigbee 3.0, works with ZHA and Zigbee2MQTT | Required |
| R7 | Cheap, hobbyist-buildable | < US$25 in parts |

> ⚠️ **This is a convenience/reminder device, not a certified fire-safety
> product.** It does not replace a smoke alarm or never leaving candles
> unattended.

## 2. Sensing options considered

| Method | How it works | Pros | Cons | Verdict |
|---|---|---|---|---|
| Visible light sensor (LDR/BH1750) | Brightness | Cheap | Fooled by any room light | ✗ |
| **Near-IR phototransistor + flicker analysis** | Flames emit strongly at 800–1100 nm and flicker at 1–15 Hz | Cheap, fast, sees through jar glass | Bright IR sources (sun) raise level | ✓ Primary |
| **Thermopile IR thermometer (MLX90614-DCI)** | Measures temperature of flame / jar glass | Ignores light entirely; very robust against sun/lamps | Slower; glass blocks long-wave IR (measures the *glass*, which is fine) | ✓ Secondary (optional) |
| UV flame sensor (UVTron R9533) | Detects 185–260 nm UV | Very selective | ~US$50+, needs HV driver; candles emit little UV | ✗ |
| Thermistor above flame | Hot air | Trivial | Must sit in the plume: fire risk, melts | ✗ |
| Camera + ML | Vision | Very flexible | Overkill, power, privacy, not Zigbee-friendly | ✗ |

**Chosen:** near-IR phototransistor (primary) + optional MLX90614 thermopile
(confirmation). The two fail in *different* ways, so combining them gives
robust detection.

## 3. System architecture

```
 ┌──────────────┐  near-IR   ┌───────────────┐  analog  ┌──────────────────────────┐
 │   Candle     │ ─────────▶ │ TEFT4300 + RC │ ───────▶ │ ESP32-C6 (XIAO)          │
 │   flame      │            │ filter 16 Hz  │  200 Hz  │                          │
 │              │  thermal   ┌───────────────┐   I²C    │  FlameDetector           │   Zigbee 3.0
 │              │ ─────────▶ │ MLX90614-DCI  │ ───────▶ │   level / flicker /      │ ─────────────▶ Coordinator
 └──────────────┘            └───────────────┘   2 Hz   │   thermal → vote → debounce │  (router)    (ZHA / Z2M)
                                                        └──────────────────────────┘
```

### Why ESP32-C6?

- Native 802.15.4 radio with Espressif's Zigbee 3.0 stack, programmable from the
  Arduino IDE (core ≥ 3.2) — no proprietary SDKs or TI/Silabs toolchains.
- The Seeed XIAO ESP32C6 is tiny (21 × 17.5 mm), has USB-C and an onboard LED/button.
- Alternatives: ESP32-H2 (Zigbee-only, lower power; same firmware works),
  nRF52840 / EFR32MG24 (better for coin-cell designs but harder toolchains).

### Power: USB, Zigbee router

Flicker detection needs continuous 200 Hz sampling, which does not suit a
sleepy battery end device. USB power also lets the device act as a **Zigbee
router**, strengthening your mesh. A battery variant is discussed in §8.

## 4. Detection algorithm

Implemented in [`firmware/candle_sensor/FlameDetector.h`](../firmware/candle_sensor/FlameDetector.h)
(platform-independent, unit-tested on a PC).

### 4.1 Signal chain (per 200 Hz sample)

1. **Anti-alias (hardware):** RC low-pass at ~16 Hz removes 100/120 Hz mains
   lamp flicker before the ADC.
2. **Low-pass (software):** two cascaded single-pole filters at 15 Hz — the
   upper edge of the flicker band, and extra rejection of any mains residue.
3. **DC tracker:** a *double* EMA (2·e₁ − e₂) at 0.3 Hz. Unlike a plain EMA it
   follows slow ramps (clouds, dimmers, sunrise) with no lag, so they don't
   leak into the flicker band.
4. **Flicker energy:** `ac = lowpass − dc`, RMS averaged over ~2 s.
5. **Features**
   - `level = dc − baseline` (baseline = stored dark reading)
   - `flicker ratio = rms / level` (modulation index — independent of distance)
   - `thermal Δ = object temp − sensor ambient temp` (if MLX90614 fitted)

### 4.2 Decision

Every 100 ms, each feature is compared against its own **hysteresis** pair:

| Evidence | ON when | OFF when |
|---|---|---|
| Level | ≥ 150 counts | ≤ 80 counts |
| Flicker | ratio ≥ 0.8 % and rms ≥ 1.5 counts | ratio ≤ 0.4 % |
| Thermal | Δ ≥ 12 °C | Δ ≤ 6 °C |

- **Optical only:** lit = *level AND flicker*. Sunlight is bright but steady;
  a TV/lamp has no 0.3–15 Hz flicker at IR.
- **With thermometer:** lit = *any 2 of 3*. This catches jar candles whose
  glass smooths the flicker (level + thermal), and still rejects sunlight
  (level only).

The candidate state must then persist for **3 s (on)** or **10 s (off)**
before it is reported, suppressing blips from a guttering flame or someone
walking past.

### 4.3 Why flicker works

A candle flame is a buoyant diffusion flame that oscillates ("flickers")
naturally at about 10–12 Hz, plus slower wandering from air currents.
Artificial lights either don't flicker (daylight, DC LEDs) or flicker at
100/120 Hz and above, which the filters remove. So energy in the 0.3–15 Hz band
relative to the IR level is a strong flame signature.

### 4.4 Simulated results (host tests)

`make -C firmware/test` runs these synthetic scenarios:

| Scenario | Expected | Result |
|---|---|---|
| Dark room | out | ✓ |
| Candle lit (3 % multi-tone flicker) | lit within 8 s | ✓ (~3.2 s) |
| Candle blown out | out after ~10 s hold | ✓ (~11.3 s) |
| Bright sunlight with slow cloud drift | out | ✓ (flicker 0.06 %) |
| Incandescent lamp, 100 Hz residue | out | ✓ (flicker 0.17 %) |
| Jar candle: weak flicker + warm glass | lit (level + thermal) | ✓ |
| Sunlight + cool thermal target | out (1 of 3 votes) | ✓ |

These are synthetic; thresholds should be confirmed with real captures
(see §7).

## 5. Zigbee interface

Device: manufacturer `DIY`, model `CandleSensor`, router, Zigbee 3.0.

| Endpoint | Cluster | Attribute | Meaning | Reporting |
|---|---|---|---|---|
| 10 | Binary Input (0x000F) | presentValue | Candle lit | On change + every 5 min |
| 11 | Analog Input (0x000C) | presentValue | Flame intensity 0–100 % | 10 s–5 min, Δ 2 % |
| 12 | Temperature Measurement (0x0402) | measuredValue | IR object temp (°C) | 10 s–5 min, Δ 1 °C |

Standard clusters mean **ZHA** exposes them automatically. For
**Zigbee2MQTT** use the external converter in
[`integrations/zigbee2mqtt/candle_sensor.mjs`](../integrations/zigbee2mqtt/candle_sensor.mjs).

## 6. User interface

| Action | Result |
|---|---|
| Power on, not paired | LED blinks slowly while searching; put coordinator in pairing mode |
| LED solid on | Candle detected as lit |
| Hold BOOT 1–5 s (candle **unlit**) | Stores current reading as dark baseline; LED blinks 3× |
| Hold BOOT > 8 s | Leaves the Zigbee network (factory reset) and reboots |

## 7. Calibration & tuning

1. Mount the sensor in its final position, candle unlit, normal room lighting.
2. Hold BOOT for ~2 s to store the dark baseline.
3. For tuning, set `DEBUG_CSV 1` in `config.h`, rebuild, open *Tools → Serial
   Plotter*, and observe `level`, `flicker_x1000` and `dT` with the candle lit,
   unlit, in daylight and with lamps on. Adjust the thresholds in
   `FlameDetectorConfig` if needed.

## 8. Future improvements

- **Battery variant:** ESP32-H2 or nRF52840 as a sleepy end device, waking every
  30 s to capture a 2 s burst at 200 Hz (≈7 % duty), or use thermal-only
  sensing with a 1-minute poll for multi-month battery life.
- **Multiple candles:** a wider-FOV thermopile array (MLX90640) could track
  several flames at once.
- **Zigbee-configurable thresholds** via a custom manufacturer cluster.
- **Burn-time counter** attribute (total hours) for candle-life tracking.
- **OTA** firmware updates via the Zigbee OTA cluster.
- **PCB:** a single small board replacing the XIAO + modules.
