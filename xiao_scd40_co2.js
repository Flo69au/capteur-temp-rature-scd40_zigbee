// Convertisseur externe Zigbee2MQTT pour le capteur DIY
// XIAO ESP32-C6 + SCD40 (CO2 / température / humidité)
//
// Installation :
//   1. Copier ce fichier dans le dossier "external_converters/" situé
//      à côté de configuration.yaml (le créer s'il n'existe pas).
//   2. Dans configuration.yaml, s'assurer que :
//        advanced:
//          enable_external_js: true
//      (obligatoire depuis Zigbee2MQTT 2.11+, pour des raisons de sécurité)
//   3. Redémarrer Zigbee2MQTT, puis appairer le capteur (permit join),
//      ou re-déclencher l'interview si le device apparaît déjà comme
//      "Unsupported".
//
// Le "zigbeeModel"/"model" ci-dessous doit correspondre exactement à
// ZB_MANUFACTURER / ZB_MODEL définis dans le sketch Arduino
// (setManufacturerAndModel), sinon Z2M ne fera pas le lien avec ce fichier.
//
// "ota: true" expose le bouton/entité de mise à jour dans Z2M/Home Assistant.
// Pour que Z2M trouve réellement une image, il faut en plus lui indiquer un
// index OTA (configuration.yaml -> ota.zigbee_ota_override_index_location)
// = https://raw.githubusercontent.com/Flo69au/capteur-temp-rature-scd40_zigbee/main/ota_index.json
// (voir le README et .github/workflows/ota-release.yml).

import {temperature, humidity, co2} from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['XIAO-SCD40-CO2'],
    model: 'XIAO-SCD40-CO2',
    vendor: 'Florian-DIY',
    description: 'Capteur maison CO2 / température / humidité (XIAO ESP32-C6 + SCD40)',
    ota: true,
    extend: [
        // mêmes réglages (0,30,0) : rapport inconditionnel toutes les 30s
        temperature({reporting: {min: 0, max: 30, change: 0}}),
        humidity({reporting: {min: 0, max: 30, change: 0}}),
        // min:0, max:30, change:0 -> aligné sur le firmware qui envoie un
        // rapport inconditionnel toutes les 30s (setReporting(0,30,0) côté sketch)
        co2({reporting: {min: 0, max: 30, change: 0}}),
    ],
};
