// Zigbee2MQTT external converter — Capteur CO2 SCD40
// Ajoutez ce fichier dans la config Z2M :
//   external_converters:
//     - /chemin/vers/z2m_converter.js

const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const e = exposes.presets;

// Le cluster CO2 (0x040D) envoie une valeur float entre 0 et 1.
// Le firmware divise les ppm par 1 000 000 avant d'envoyer.
const fzCO2 = {
    cluster: 'msCO2',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        if (msg.data.hasOwnProperty('measuredValue')) {
            return { co2: Math.round(msg.data['measuredValue'] * 1000000) };
        }
    },
};

const definition = {
    zigbeeModel: ['CO2 Sensor'],
    model: 'SCD40-Zigbee',
    vendor: 'FlorianL',
    description: 'Capteur CO2 / température / humidité SCD40 Zigbee',
    image: 'https://raw.githubusercontent.com/Flo69au/capteur-temp-rature-scd40_zigbee/main/images/capteur-temp.png',
    fromZigbee: [fz.temperature, fz.humidity, fzCO2],
    toZigbee: [],
    ota: true,
    exposes: [e.temperature(), e.humidity(), e.co2()],
};

module.exports = definition;
