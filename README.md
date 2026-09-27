# Smart Candle Sensor 🕯️

A DIY **Zigbee 3.0** sensor that watches a candle and tells your smart home
whether it is **lit** or **out**. Use it to get alerts when a candle is left
burning while nobody is home, at bedtime, or for too long.

> ⚠️ A convenience/reminder device, **not** a certified fire-safety product.
> Never leave candles unattended, and keep your smoke alarms working.

## How it works

- A **near-IR phototransistor** (with daylight filter) watches the flame. The
  firmware looks for both **brightness** and the characteristic **0.3–15 Hz
  flicker** of a flame, which rejects sunlight and mains-powered lamps.
- An optional **MLX90614 IR thermometer** measures the flame / jar glass
  temperature as a second opinion (any 2 of 3 signals ⇒ lit).
- An **ESP32-C6** reports the state over Zigbee as standard clusters, so it
  works with **ZHA** out of the box and with **Zigbee2MQTT** via an external
  converter. USB-powered, it also acts as a Zigbee router.

Full rationale, algorithm and alternatives: **[docs/design.md](docs/design.md)**.

## Repository layout

```
docs/design.md                         Design document
hardware/bom.csv                       Bill of materials
hardware/wiring.md                     Schematic, pinout, mounting
firmware/candle_sensor/                Arduino sketch (ESP32-C6)
  candle_sensor.ino                    Zigbee device + sampling loop
  FlameDetector.h                      Detection algorithm (portable)
  config.h                             Pins, timings, options
firmware/test/                         Host unit tests for FlameDetector
integrations/zigbee2mqtt/              Z2M external converter
integrations/home-assistant/           Example automations
```

## Build it

### Hardware (≈ US$15–25)

| Part | Example |
|---|---|
| MCU | Seeed Studio XIAO ESP32C6 |
| IR sensor | Vishay TEFT4300 phototransistor + 22 kΩ, 10 kΩ, 1 µF |
| Thermometer (optional) | MLX90614ESF-**DCI** (5° FOV) / GY-906-DCI |
| Power | Any USB-C 5 V supply |

See [hardware/wiring.md](hardware/wiring.md) for the schematic and mounting
(sensor 20–40 cm from the candle, aimed at the flame, never above it).

### Firmware

1. Arduino IDE with **esp32 by Espressif** core **≥ 3.2**.
2. Library: **Adafruit MLX90614** (or set `USE_MLX90614 0` in `config.h`).
3. Tools menu:
   - Board: `XIAO_ESP32C6`
   - Zigbee mode: `Zigbee ZCZR (coordinator/router)`
   - Partition scheme: `Zigbee ZCZR 4MB with spiffs`
4. Open `firmware/candle_sensor/candle_sensor.ino` and upload.

Or with `arduino-cli`:

```sh
arduino-cli compile --upload -p /dev/ttyACM0 \
  --fqbn esp32:esp32:XIAO_ESP32C6:ZigbeeMode=zczr,PartitionScheme=zigbee_zczr \
  firmware/candle_sensor
```

### Algorithm tests (no hardware needed)

```sh
make -C firmware/test
```

### Pair & calibrate

1. Enable pairing in ZHA / Zigbee2MQTT, then power the sensor. The LED blinks
   while joining.
2. With the candle **unlit** and the sensor in place, hold **BOOT** for ~2 s to
   store the dark baseline (LED blinks 3×).
3. Light the candle — within a few seconds the LED turns on and the
   *flame* entity turns **on**.

Hold BOOT > 8 s to leave the network.

## Exposed entities

| Entity | Cluster (endpoint) | Notes |
|---|---|---|
| Flame (binary) | Binary Input (10) | `on` = lit |
| Flame intensity (%) | Analog Input (11) | Relative IR brightness |
| Temperature (°C) | Temperature Measurement (12) | IR thermometer, if fitted |

Zigbee2MQTT: copy `integrations/zigbee2mqtt/candle_sensor.mjs` into your Z2M
`external_converters/` folder and restart.

Home Assistant automation examples:
[integrations/home-assistant/automations.yaml](integrations/home-assistant/automations.yaml).
