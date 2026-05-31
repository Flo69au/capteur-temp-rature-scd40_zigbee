const m = require('zigbee-herdsman-converters/lib/modernExtend');

module.exports = {
    zigbeeModel: ['CO2 Sensor'],
    model: 'SCD40-ZB-01',
    vendor: 'FlorianL',
    description: 'Capteur CO2 / température / humidité SCD40 Zigbee',
    image: 'https://raw.githubusercontent.com/Flo69au/capteur-temp-rature-scd40_zigbee/main/images/capteur-temp.png',
    extend: [m.temperature(), m.humidity(), m.co2()],
    ota: true,
    configure: async (device, coordinatorEndpoint, logger) => {
        // Le firmware pousse les données lui-même — pas de configReport nécessaire
    },
};
