// config.h - pins and tunables for the Zigbee candle sensor.
#pragma once

// ---- Board: Seeed Studio XIAO ESP32C6 ------------------------------------
#define PIN_IR_ADC 0        // D0 / A0 - IR phototransistor (via RC filter)
#define PIN_I2C_SDA 22      // D4 - MLX90614 SDA
#define PIN_I2C_SCL 23      // D5 - MLX90614 SCL
#define PIN_BUTTON 9        // BOOT button (active low)
#define PIN_LED 15          // on-board user LED (active low)

// ---- Optional IR thermometer ---------------------------------------------
// Set to 0 to build an optical-only sensor.
#define USE_MLX90614 1

// ---- Zigbee ----------------------------------------------------------------
#define ZB_MANUFACTURER "DIY"
#define ZB_MODEL "CandleSensor"
#define EP_FLAME 10         // Binary Input: candle lit
#define EP_INTENSITY 11     // Analog Input: flame brightness %
#define EP_TEMPERATURE 12   // Temperature Measurement: flame/jar temp

// ---- Timing ----------------------------------------------------------------
#define SAMPLE_RATE_HZ 200
#define OVERSAMPLE 4               // ADC reads averaged per sample
#define EVALUATE_INTERVAL_MS 100
#define THERMAL_INTERVAL_MS 500
#define ANALOG_REPORT_MS 10000     // intensity / temperature push interval
#define HEARTBEAT_REPORT_MS 300000 // re-send lit state even if unchanged

// ---- Button ----------------------------------------------------------------
#define CALIBRATE_PRESS_MS 1000    // hold 1-5 s: store dark baseline
#define FACTORY_RESET_PRESS_MS 8000 // hold >8 s: leave network

// ---- Debug -----------------------------------------------------------------
// Streams CSV (level, flicker ratio, thermal delta, lit) at 10 Hz over USB
// serial for threshold tuning with Arduino's Serial Plotter.
#define DEBUG_CSV 0
