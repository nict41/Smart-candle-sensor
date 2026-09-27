# Wiring

Target board: **Seeed Studio XIAO ESP32C6**. Everything runs from the board's 3V3 pin.

## Schematic

```
 3V3 ──┬────────────────┬───────────────────────── MLX90614 VIN
       │                │
   C2 ═╪═ 100nF         │ C (collector)
       │           Q1 TEFT4300  (IR phototransistor)
      GND               │ E (emitter)
                        ├────[ R2 10k ]────┬────── A0 / D0 (GPIO0)
                        │                  │
                   [ R1 22k ]          C1 ═╪═ 1µF
                        │                  │
                       GND                GND

 MLX90614 (optional):  GND ── GND
                       SDA ── D4 (GPIO22)
                       SCL ── D5 (GPIO23)
```

- **Q1 + R1** form an emitter-follower light sensor: more IR ⇒ higher voltage at A0.
- **R2 + C1** is a ~16 Hz low-pass (anti-alias) filter. It removes most of the
  100/120 Hz flicker from mains lighting *before* sampling, so it cannot alias
  into the 0.3–15 Hz candle-flicker band.
- GY-906 modules already include I²C pull-ups; add 4.7 kΩ to 3V3 on SDA/SCL if
  you use a bare sensor.

## Pin summary

| Signal | XIAO pin | GPIO |
|---|---|---|
| IR phototransistor (filtered) | D0 / A0 | 0 |
| MLX90614 SDA | D4 | 22 |
| MLX90614 SCL | D5 | 23 |
| Button (calibrate / reset) | BOOT | 9 |
| Status LED | on-board | 15 (active low) |

## Choosing R1

R1 sets sensitivity. Aim for the lit candle to read roughly **1000–2500 ADC
counts** (of 4095) at your mounting distance, with room lights on:

| Distance to flame | Suggested R1 |
|---|---|
| 15–25 cm | 10 kΩ |
| 25–40 cm | 22 kΩ |
| 40–60 cm | 47 kΩ |

If A0 saturates near 4095 in daylight, lower R1. Use `DEBUG_CSV 1` and the
Arduino Serial Plotter to check.

## Mechanical notes

- Put Q1 at the back of a ~20 mm long, matte-black tube (≈5 mm bore). This
  narrows its field of view to the flame and cuts stray window light.
- Aim Q1 and the MLX90614 at the flame (open candle) or at the upper third of
  the glass (jar candle).
- Keep the electronics **at least 20 cm** from the flame and **never above it**;
  hot air rises and will cook PLA and the thermometer.
