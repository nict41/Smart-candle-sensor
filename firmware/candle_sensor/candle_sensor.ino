// Zigbee candle flame sensor - ESP32-C6 (Arduino core >= 3.2)
//
// Arduino IDE settings:
//   Board:            XIAO_ESP32C6 (or ESP32C6 Dev Module)
//   Zigbee mode:      Zigbee ZCZR (coordinator/router)
//   Partition scheme: Zigbee ZCZR 4MB with spiffs
//
// Endpoints:
//   10  Binary Input        - candle lit (true/false)
//   11  Analog Input        - flame intensity, %
//   12  Temperature Sensor  - IR thermometer object temp (if USE_MLX90614)

#ifndef ZIGBEE_MODE_ZCZR
#error "Select Tools > Zigbee mode > Zigbee ZCZR (coordinator/router)"
#endif

#include <Preferences.h>
#include "Zigbee.h"
#include "config.h"
#include "FlameDetector.h"

#if USE_MLX90614
#include <Wire.h>
#include <Adafruit_MLX90614.h>
Adafruit_MLX90614 mlx;
bool mlxOk = false;
#endif

ZigbeeBinary zbFlame(EP_FLAME);
ZigbeeAnalog zbIntensity(EP_INTENSITY);
#if USE_MLX90614
ZigbeeTempSensor zbTemp(EP_TEMPERATURE);
#endif

FlameDetector detector;
Preferences prefs;

static uint32_t nextSampleUs = 0;
static uint32_t lastEvalMs = 0, lastThermalMs = 0, lastAnalogMs = 0, lastHeartbeatMs = 0;
static float lastObjectC = NAN;

static void setLed(bool on) { digitalWrite(PIN_LED, on ? LOW : HIGH); }

static float readIr() {
  uint32_t sum = 0;
  for (int i = 0; i < OVERSAMPLE; i++) sum += analogRead(PIN_IR_ADC);
  return (float)sum / OVERSAMPLE;
}

static void blink(int times, int ms) {
  for (int i = 0; i < times; i++) {
    setLed(true);
    delay(ms);
    setLed(false);
    delay(ms);
  }
}

static void calibrateBaseline() {
  float b = detector.dcLevel();
  detector.setBaseline(b);
  prefs.putFloat("baseline", b);
  Serial.printf("Dark baseline stored: %.1f counts\n", b);
  blink(3, 150);
}

static void reportLit(bool lit) {
  zbFlame.setBinaryInput(lit);
  zbFlame.reportBinaryInput();
  setLed(lit);
  Serial.printf("Candle %s (level %.0f, flicker %.4f, dT %.1f)\n", lit ? "LIT" : "OUT", detector.level(),
                detector.flickerRatio(), detector.thermalDeltaC());
}

static void handleButton() {
  static uint32_t pressedAt = 0;
  bool down = digitalRead(PIN_BUTTON) == LOW;
  if (down && pressedAt == 0) pressedAt = millis();
  if (!down && pressedAt != 0) {
    uint32_t held = millis() - pressedAt;
    pressedAt = 0;
    if (held >= CALIBRATE_PRESS_MS && held < FACTORY_RESET_PRESS_MS) calibrateBaseline();
  }
  if (down && pressedAt != 0 && millis() - pressedAt >= FACTORY_RESET_PRESS_MS) {
    Serial.println("Factory reset: leaving Zigbee network");
    blink(10, 50);
    Zigbee.factoryReset();  // reboots
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  setLed(false);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_IR_ADC, ADC_11db);

  FlameDetectorConfig cfg;
  cfg.sampleRateHz = SAMPLE_RATE_HZ;
  detector.configure(cfg);

  prefs.begin("candle", false);
  detector.setBaseline(prefs.getFloat("baseline", 0.0f));
  Serial.printf("Dark baseline: %.1f counts\n", detector.baseline());

#if USE_MLX90614
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  mlxOk = mlx.begin(MLX90614_I2CADDR, &Wire);
  Serial.printf("MLX90614 %s\n", mlxOk ? "found" : "NOT found - optical only");
#endif

  // --- Zigbee endpoints ---
  zbFlame.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
  zbFlame.addBinaryInput();
  zbFlame.setBinaryInputDescription("Candle lit");
  Zigbee.addEndpoint(&zbFlame);

  zbIntensity.addAnalogInput();
  zbIntensity.setAnalogInputApplication(ESP_ZB_ZCL_AI_PERCENTAGE_OTHER);
  zbIntensity.setAnalogInputDescription("Flame intensity %");
  zbIntensity.setAnalogInputResolution(1);
  Zigbee.addEndpoint(&zbIntensity);

#if USE_MLX90614
  zbTemp.setMinMaxValue(-20, 380);
  zbTemp.setTolerance(1);
  Zigbee.addEndpoint(&zbTemp);
#endif

  // Mains (USB) powered, so run as a router and strengthen the mesh.
  if (!Zigbee.begin(ZIGBEE_ROUTER)) {
    Serial.println("Zigbee failed to start, rebooting");
    delay(1000);
    ESP.restart();
  }
  Serial.print("Joining network");
  while (!Zigbee.connected()) {
    Serial.print('.');
    setLed(true);
    delay(100);
    setLed(false);
    delay(400);
  }
  Serial.println(" joined");

  zbIntensity.setAnalogInputReporting(10, 300, 2);  // min s, max s, delta %
#if USE_MLX90614
  zbTemp.setReporting(10, 300, 1);                   // min s, max s, delta C
#endif

  reportLit(false);
  nextSampleUs = micros();
}

void loop() {
  // Fixed-rate sampling; everything else is non-blocking.
  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - nextSampleUs) >= 0) {
    nextSampleUs += 1000000UL / SAMPLE_RATE_HZ;
    detector.addSample(readIr());
  }

  uint32_t now = millis();

#if USE_MLX90614
  if (mlxOk && now - lastThermalMs >= THERMAL_INTERVAL_MS) {
    lastThermalMs = now;
    float obj = mlx.readObjectTempC();
    float amb = mlx.readAmbientTempC();
    if (!isnan(obj) && !isnan(amb)) {
      detector.addThermal(obj, amb);
      lastObjectC = obj;
    }
  }
#endif

  if (now - lastEvalMs >= EVALUATE_INTERVAL_MS) {
    lastEvalMs = now;
    if (detector.evaluate(now)) {
      reportLit(detector.lit());
      lastHeartbeatMs = now;
    }
#if DEBUG_CSV
    Serial.printf("level:%.1f,flicker_x1000:%.2f,dT:%.1f,lit:%d\n", detector.level(), detector.flickerRatio() * 1000.0f,
                  detector.thermalDeltaC(), detector.lit() ? 100 : 0);
#endif
  }

  if (now - lastAnalogMs >= ANALOG_REPORT_MS) {
    lastAnalogMs = now;
    zbIntensity.setAnalogInput(detector.intensityPercent());
#if USE_MLX90614
    if (!isnan(lastObjectC)) zbTemp.setTemperature(lastObjectC);
#endif
  }

  if (now - lastHeartbeatMs >= HEARTBEAT_REPORT_MS) {
    lastHeartbeatMs = now;
    zbFlame.reportBinaryInput();
  }

  handleButton();
}
