// Zigbee2MQTT external converter for the DIY candle sensor.
// Copy to <z2m data>/external_converters/ and restart Zigbee2MQTT (v2.x).
//
// Endpoint 10 genBinaryInput  -> "flame"            (true when lit)
// Endpoint 11 genAnalogInput  -> "flame_intensity"  (%)
// Endpoint 12 msTemperatureMeasurement -> "temperature" (IR thermometer)

import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
  zigbeeModel: ['CandleSensor'],
  model: 'CandleSensor',
  vendor: 'DIY',
  description: 'Zigbee candle flame sensor (ESP32-C6)',
  extend: [
    m.binary({
      name: 'flame',
      cluster: 'genBinaryInput',
      attribute: 'presentValue',
      valueOn: [true, 1],
      valueOff: [false, 0],
      description: 'Candle is lit',
      access: 'STATE_GET',
      reporting: {min: 0, max: 300, change: 1},
    }),
    m.numeric({
      name: 'flame_intensity',
      cluster: 'genAnalogInput',
      attribute: 'presentValue',
      unit: '%',
      precision: 0,
      description: 'Near-IR brightness of the flame',
      access: 'STATE_GET',
      reporting: {min: 10, max: 300, change: 2},
    }),
    m.temperature(),
  ],
};
