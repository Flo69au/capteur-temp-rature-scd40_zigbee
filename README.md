# Capteur CO2 / température / humidité Zigbee — XIAO ESP32-C6 + SCD40

Firmware Arduino pour un Seeed XIAO ESP32-C6 + Sensirion SCD40, en **routeur Zigbee** (alimentation secteur), pour Zigbee2MQTT.

- Température + humidité (endpoint 10) et CO2 (endpoint 11), envoyés toutes les 30 s
- Calibrage automatique du CO2 (ASC, 400 ppm) et offset de température réglable (`TEMP_OFFSET_C`)
- Bouton d'appairage (D1 → GND), LED de statut (GPIO15)
- Mise à jour OTA par le réseau Zigbee, publiée automatiquement par GitHub Actions

## Câblage

| SCD40 | XIAO ESP32-C6 |
|---|---|
| VDD | 3V3 |
| GND | GND |
| SDA | D4 (GPIO22) |
| SCL | D5 (GPIO23) |
| Bouton d'appairage | D1 (GPIO1) → GND |

## Compilation (Arduino IDE)

- Core **esp32 by Espressif** ≥ 3.3, librairie **Sensirion I2C SCD4x**
- Outils → Carte : `XIAO_ESP32C6`
- Outils → Zigbee mode : `Zigbee ZCZR (coordinator/router)`
- Outils → Partition Scheme : `Zigbee ZCZR 4MB with spiffs`
- Passage d'un firmware *end device* au routeur : **une fois en USB** avec *Erase All Flash = Enabled*,
  puis réappairer dans Z2M (la table des partitions change, ce passage ne peut pas se faire en OTA)

`i2c_scanner/` : petit sketch de diagnostic (le SCD40 doit répondre en `0x62`).

## Zigbee2MQTT

1. Copier `z2m_converter.js` dans `external_converters/` (à côté de `configuration.yaml`)
2. Dans `configuration.yaml` :
   ```yaml
   advanced:
     enable_external_js: true
   ota:
     zigbee_ota_override_index_location: https://raw.githubusercontent.com/Flo69au/zigbee-ota/main/ota_index.json
     disable_automatic_update_check: false   # Z2M détecte seul les nouvelles versions
     update_check_interval: 1440             # au plus une vérification par 24 h
     default_maximum_data_size: 50           # ne PAS mettre 100 : l'OTA échoue dès le début
   ```
   L'index commun [zigbee-ota](https://github.com/Flo69au/zigbee-ota) fusionne toutes les heures le
   `ota_index.json` de ce dépôt avec ceux du Levoit et de la sonde radiateur.
3. Redémarrer Zigbee2MQTT

## Publier une mise à jour OTA

1. Commit + push des modifications du sketch
2. Créer un tag **`v<majeur>.<mineur>`** plus grand que le précédent (`v1.0`, `v1.1`, `v1.2`…, ou
   `v1.2.1` pour un correctif), en le poussant ou depuis GitHub : *Releases → Draft a new release →
   nouveau tag → Publish release*. Penser à mettre la même valeur dans `OTA_FW_VERSION`
   (valeur par défaut du sketch pour un flash USB, format `0xMMmmPP00` : v1.1 → `0x01010000`)
3. Le workflow `.github/workflows/ota-release.yml` compile avec `OTA_FW_VERSION` = version du tag,
   crée l'image `.zigbee`, la publie dans une Release et met à jour `ota_index.json`
4. Dans Zigbee2MQTT, la nouvelle version apparaît d'elle-même dans les 24 h (ou tout de suite avec
   *Check for updates*) ; l'installation se lance avec *Update* (~1 h, ne pas redémarrer Z2M pendant)

Pas de `"force": true` dans l'index : la mise à jour serait proposée en boucle.
