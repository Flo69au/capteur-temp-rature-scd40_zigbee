const exposes = require('zigbee-herdsman-converters/lib/exposes');
const e = exposes.presets;

const fromZigbeeTemperature = {
    cluster: 'msTemperatureMeasurement',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        if (msg.data['measuredValue'] !== undefined) {
            return {temperature: parseFloat((msg.data['measuredValue'] / 100).toFixed(2))};
        }
    },
};

const fromZigbeeHumidity = {
    cluster: 'msRelativeHumidity',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        if (msg.data['measuredValue'] !== undefined) {
            return {humidity: parseFloat((msg.data['measuredValue'] / 100).toFixed(2))};
        }
    },
};

const fromZigbeeCO2 = {
    cluster: 'msCO2',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        if (msg.data['measuredValue'] !== undefined) {
            return {co2: Math.round(msg.data['measuredValue'] * 1000000)};
        }
    },
};

const definition = {
    zigbeeModel: ['CO2 Sensor'],
    model: 'SCD40-Zigbee',
    vendor: 'FlorianL',
    description: 'Capteur CO2 / température / humidité SCD40 Zigbee',
    image: 'https://raw.githubusercontent.com/Flo69au/capteur-temp-rature-scd40_zigbee/main/images/capteur-temp.png',
    fromZigbee: [fromZigbeeTemperature, fromZigbeeHumidity, fromZigbeeCO2],
    toZigbee: [],
    ota: true,
    configure: async (device, coordinatorEndpoint) => {},
    exposes: [e.temperature(), e.humidity(), e.co2()],
};

module.exports = definition;
