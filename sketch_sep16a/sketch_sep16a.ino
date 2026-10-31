/*
 * Capteur Zigbee CO2 / Température / Humidité
 * XIAO ESP32-C6 + Sensirion SCD40 (I2C)
 *
 * Bibliothèque à installer (Gestionnaire de bibliothèques Arduino) :
 *   - "Sensirion I2C SCD4x" (auteur: Sensirion AG)
 *   La bibliothèque Zigbee est fournie nativement par le core ESP32
 *   (Boards Manager -> "esp32" by Espressif Systems, version 3.3.x ou plus récente).
 *
 * Réglages Outils (Arduino IDE) AVANT de flasher :
 *   - Carte                                   : XIAO_ESP32C6
 *   - Zigbee mode                             : Zigbee ED (end device)
 *   - Partition Scheme                        : Zigbee 4MB with spiffs
 *   - Erase All Flash Before Sketch Upload    : Enabled (au moins pour le 1er flash,
 *                                                ou après un factory reset du coordinateur)
 *
 * Câblage SCD40 -> XIAO ESP32-C6 :
 *   VDD -> 3V3
 *   GND -> GND
 *   SDA -> D4  (GPIO22)
 *   SCL -> D5  (GPIO23)
 *
 * Bouton d'appairage (optionnel, en plus du bouton BOOT) :
 *   Un simple bouton-poussoir entre D1 (GPIO1) et GND. Utilise la pull-up
 *   interne, donc pas besoin de résistance externe. Un appui déclenche un
 *   factory reset Zigbee (le device redevient "non appairé" et rejoint le
 *   1er réseau ouvert détecté). Change PAIR_BUTTON ci-dessous si tu préfères
 *   une autre broche libre (D2, D3...).
 *
 * LED de statut : la LED orange intégrée au XIAO ESP32-C6 (GPIO15,
 *   active LOW -> LOW = allumée) clignote tant que le device n'est pas
 *   connecté au réseau Zigbee (donc pendant l'appairage) et s'éteint une
 *   fois connecté. Si elle s'allume à l'envers chez toi, inverse simplement
 *   ledOn()/ledOff() plus bas.
 *
 * Côté Zigbee2MQTT : ce firmware n'étant pas un device "connu", il faut lui
 * associer le convertisseur externe du repo (z2m_converter.js).
 *
 * Mise à jour OTA (par le réseau Zigbee, pas de WiFi nécessaire) :
 *   Le device embarque un client OTA Zigbee natif.
 *
 *   Méthode recommandée : GitHub (automatique)
 *     Pousser un tag "v2", "v3"... sur le repo GitHub : le workflow
 *     .github/workflows/ota-release.yml compile ce sketch avec
 *     OTA_FW_VERSION = numéro du tag, fabrique l'image .zigbee, la publie
 *     dans une Release et met à jour ota_index.json, que Zigbee2MQTT lit via
 *     ota.zigbee_ota_override_index_location (URL raw.githubusercontent.com).
 *
 *   Méthode manuelle (sans GitHub) :
 *     1. Sketch -> "Export compiled binary" dans l'Arduino IDE (.bin)
 *     2. python image_builder_tool.py --manuf-id 0x1337 --image-type 0x0001 \
 *          --file-version 0x00000002 --tag 0x0000 sketch_sep16a.ino.bin
 *        (outil Espressif : esp-zigbee-sdk/tools/image_builder_tool/)
 *        -> produit 1337-0001-00000002-ota-file.zigbee
 *     3. Le déclarer dans un index JSON lu par Zigbee2MQTT (voir le guide).
 *
 *   Ne JAMAIS mettre "force": true dans l'index : Z2M proposerait alors la
 *   mise à jour en permanence, même si la version est déjà installée.
 *   Le device vérifie automatiquement les mises à jour ~1 min après connexion
 *   puis toutes les heures. Pendant une mise à jour, la LED clignote vite et
 *   les boutons de reset sont désactivés pour éviter de couper l'update.
 */

#include <Arduino.h>

#ifndef ZIGBEE_MODE_ED
#error "Sélectionne 'Zigbee ED (end device)' dans Outils -> Zigbee mode"
#endif

#include "Zigbee.h"
#include <Wire.h>
#include <SensirionI2cScd4x.h>

// ---- Configuration ----
#define TEMP_HUM_ENDPOINT 10
#define CO2_ENDPOINT      11
#define I2C_SDA           D4
#define I2C_SCL           D5
#define PAIR_BUTTON       D1  // bouton dédié à l'appairage (vers GND)
#define STATUS_LED        LED_BUILTIN  // LED orange intégrée (GPIO15, active LOW)

// Doivent correspondre au "zigbeeModel"/"model" du convertisseur externe Z2M
#define ZB_MANUFACTURER "Florian-DIY"
#define ZB_MODEL        "XIAO-SCD40-CO2"

// ---- OTA ----
// Version de CE firmware. Pour une release GitHub, elle est imposée depuis le
// tag (v10 -> 10) via -DOTA_FW_VERSION. La valeur par défaut ci-dessous sert au
// flash USB depuis l'Arduino IDE : la garder ÉGALE au dernier tag publié, pour
// que Zigbee2MQTT ne repropose pas une mise à jour déjà installée.
#ifndef OTA_FW_VERSION
#define OTA_FW_VERSION       0x0000000A
#endif
#define OTA_HW_VERSION       0x0101      // version matérielle (rarement à changer)
#define OTA_MANUFACTURER_ID  0x1337      // arbitraire, doit matcher l'image .zigbee généré
#define OTA_IMAGE_TYPE       0x0001      // arbitraire, doit matcher l'image .zigbee généré

// Offset de température (°C) -> corrige aussi l'humidité (voir setup()).
// Valeur usine par défaut = 4.0°C.
// Formule : nouvel_offset = T_capteur - T_reference + offset_installé
// Historique : 2.5 -> 6.2 (29/10) -> 8.9 (31/10 : capteur 23.5°C vs réf 20.8°C
// avec 6.2 -> 23.5 - 20.8 + 6.2 = 8.9). Valeur confirmée par Florian.
#define TEMP_OFFSET_C 8.9

// Correction d'humidité (en points de %RH), appliquée par le firmware avant
// l'envoi Zigbee. Le SCD40 n'a pas de calibrage RH interne.
// Formule : HUM_OFFSET_PCT = RH_reference - RH_affichée + ancien offset
// 1er réglage : capteur 56.6% vs référence 47.2% -> -9.4
// Réajusté le 2026-10-29 : affiché 45.4% vs référence 47.2% -> -7.6
#define HUM_OFFSET_PCT -7.6

// Envoi forcé des 3 mesures vers Zigbee2MQTT, quoi qu'il arrive (même si la
// valeur n'a pas changé). Le reporting automatique de la pile Zigbee ne
// garantit pas un envoi régulier, donc le firmware l'impose lui-même.
#define REPORT_INTERVAL_MS 30000UL

uint8_t button = BOOT_PIN;
volatile bool otaRunning = false;
volatile bool otaEnded = false;          // OTA terminée sans redémarrage auto (= échec)
volatile unsigned long otaStartMs = 0;

// Garde-fou OTA : la lib Zigbee ne prévient pas le sketch quand une mise à jour
// est annulée (ABORT), et garde alors un état interne incohérent qui fait
// échouer les tentatives suivantes. Si l'OTA dure plus que ça, ou se termine
// sans redémarrer, le firmware redémarre pour repartir sur un état propre.
#define OTA_MAX_DURATION_MS (2UL * 60UL * 60UL * 1000UL)  // 2 h

// Petits helpers pour la LED active LOW (LOW = allumée sur ce board)
inline void ledOn() {
  digitalWrite(STATUS_LED, LOW);
}
inline void ledOff() {
  digitalWrite(STATUS_LED, HIGH);
}

void onOTAStateChange(bool active) {
  if (active) {
    otaStartMs = millis();
    otaEnded = false;
  } else if (otaRunning) {
    otaEnded = true;  // en cas de succès la lib redémarre d'elle-même juste après
  }
  otaRunning = active;
  Serial.println(active ? "OTA: mise à jour démarrée" : "OTA: terminée");
}

SensirionI2cScd4x scd40;
ZigbeeTempSensor zbTempHum = ZigbeeTempSensor(TEMP_HUM_ENDPOINT);
ZigbeeCarbonDioxideSensor zbCO2 = ZigbeeCarbonDioxideSensor(CO2_ENDPOINT);

static char errorMessage[64];

void setup() {
  Serial.begin(115200);
  delay(2000);  // laisse le temps d'ouvrir le moniteur série en debug
  Serial.printf("Firmware version %lu\r\n", (unsigned long)OTA_FW_VERSION);

  pinMode(button, INPUT_PULLUP);
  pinMode(PAIR_BUTTON, INPUT_PULLUP);
  pinMode(STATUS_LED, OUTPUT);
  ledOff();

  // ---------------- I2C / SCD40 ----------------
  Wire.begin(I2C_SDA, I2C_SCL);
  scd40.begin(Wire, SCD40_I2C_ADDR_62);

  uint16_t error;
  scd40.wakeUp();
  scd40.stopPeriodicMeasurement();
  scd40.reinit();

  uint64_t serialNumber = 0;
  error = scd40.getSerialNumber(serialNumber);
  if (error) {
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.printf("Erreur getSerialNumber: %s\r\n", errorMessage);
  }

  // Calibrage automatique (ASC) : le SCD40 corrige tout seul sa dérive dans le
  // temps, en supposant que la valeur la plus basse vue sur une période (par
  // défaut ~6,5 jours après une 1ère période de rodage de ~44h) correspond à
  // de l'air frais (~400 ppm). Pour que ça fonctionne, la pièce doit être
  // ventilée/aérée de temps en temps. Réglage stocké en EEPROM sur le capteur
  // (persiste entre les redémarrages), mais on le force à chaque boot pour
  // être sûr qu'il est bien actif.
  error = scd40.setAutomaticSelfCalibrationEnabled(1);
  if (error) {
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.printf("Erreur setAutomaticSelfCalibrationEnabled: %s\r\n", errorMessage);
  }
  error = scd40.setAutomaticSelfCalibrationTarget(400);  // cible = air frais, 400 ppm
  if (error) {
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.printf("Erreur setAutomaticSelfCalibrationTarget: %s\r\n", errorMessage);
  }

  // Le SCD40 n'a pas de calibrage dédié pour l'humidité : la RH est calculée
  // à partir de la température mesurée en interne, qui chauffe légèrement à
  // cause de l'auto-échauffement du capteur/de l'électronique une fois dans
  // son boîtier. Corriger cet offset de température corrige donc aussi la RH
  // (et n'affecte pas la précision du CO2). Voir le guide du projet pour la
  // procédure de calibrage (comparaison avec un thermomètre de référence).
  error = scd40.setTemperatureOffset(TEMP_OFFSET_C);
  if (error) {
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.printf("Erreur setTemperatureOffset: %s\r\n", errorMessage);
  }

  error = scd40.startPeriodicMeasurement();
  if (error) {
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.printf("Erreur startPeriodicMeasurement: %s\r\n", errorMessage);
  }

  // ---------------- Endpoint Température + Humidité (EP 10) ----------------
  zbTempHum.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
  zbTempHum.setMinMaxValue(-10, 60);     // plage SCD40
  zbTempHum.setTolerance(0.2);
  zbTempHum.setDefaultValue(20.0);       // valeur affichée avant la 1ère mesure réelle
  zbTempHum.addHumiditySensor(0, 100, 1, 50.0);

  // Client OTA (une seule fois suffit, il met à jour tout le firmware du device)
  zbTempHum.addOTAClient(OTA_FW_VERSION, OTA_FW_VERSION, OTA_HW_VERSION, OTA_MANUFACTURER_ID, OTA_IMAGE_TYPE);
  zbTempHum.onOTAStateChange(onOTAStateChange);

  Zigbee.addEndpoint(&zbTempHum);

  // ---------------- Endpoint CO2 (EP 11) ----------------
  zbCO2.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
  zbCO2.setMinMaxValue(0, 5000);         // plage SCD40 (jusqu'à 40000 en mode non calibré)
  Zigbee.addEndpoint(&zbCO2);

  Serial.println("Démarrage Zigbee...");
  if (!Zigbee.begin()) {
    Serial.println("Échec du démarrage Zigbee, redémarrage...");
    ESP.restart();
  }

  Serial.print("Connexion au réseau Zigbee (appairage en cours)");
  while (!Zigbee.connected()) {
    Serial.print(".");
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));  // clignote pendant l'appairage
    delay(200);
  }
  Serial.println();
  Serial.println("Connecté au réseau Zigbee !");
  ledOff();  // appairage terminé -> LED éteinte en fonctionnement normal

  // Cadence de reporting -> DOIT être appelé après Zigbee.begin()
  // min=0, max=30, delta=0 -> envoi systématique toutes les 30s, peu importe la variation
  zbTempHum.setReporting(0, 30, 0);
  zbTempHum.setHumidityReporting(0, 30, 0);
  zbCO2.setReporting(0, 30, 0);

  // 1ère vérification OTA ~1 min après connexion, puis toutes les heures automatiquement
  zbTempHum.requestOTAUpdate();
}

void loop() {
  static unsigned long lastRead = 0;
  static unsigned long lastReport = 0;

  // Pendant une mise à jour OTA : LED clignote vite, boutons de reset désactivés
  // (couper l'alimentation ou remettre à zéro le réseau en plein flash OTA = brique)
  if (otaEnded) {
    Serial.println("OTA interrompue -> redémarrage pour repartir sur un état propre");
    delay(2000);
    ESP.restart();
  }
  if (otaRunning) {
    if (millis() - otaStartMs > OTA_MAX_DURATION_MS) {
      Serial.println("OTA bloquée depuis trop longtemps -> redémarrage");
      delay(500);
      ESP.restart();
    }
    digitalWrite(STATUS_LED, (millis() / 100) % 2);
    delay(10);
    return;
  }

  // Appui long (>5s) sur le bouton BOOT = factory reset Zigbee (re-appairage)
  if (digitalRead(button) == LOW) {
    delay(100);
    unsigned long start = millis();
    while (digitalRead(button) == LOW && !otaRunning) {
      delay(50);
      if (millis() - start > 5000) {
        Serial.println("Factory reset Zigbee...");
        ledOn();  // confirmation visuelle avant le redémarrage
        delay(500);
        Zigbee.factoryReset();
      }
    }
  }

  // Bouton dédié à l'appairage : simple appui court -> factory reset Zigbee
  if (digitalRead(PAIR_BUTTON) == LOW) {
    delay(50);  // anti-rebond
    if (digitalRead(PAIR_BUTTON) == LOW) {
      Serial.println("Bouton d'appairage pressé -> factory reset Zigbee...");
      ledOn();  // confirmation visuelle avant le redémarrage
      delay(500);
      Zigbee.factoryReset();
      while (digitalRead(PAIR_BUTTON) == LOW) {
        delay(10);  // attend le relâchement du bouton
      }
    }
  }

  // Le SCD40 produit une nouvelle mesure toutes les ~5s en mode périodique
  if (millis() - lastRead > 1000) {
    lastRead = millis();
    bool dataReady = false;
    uint16_t error = scd40.getDataReadyStatus(dataReady);
    if (!error && dataReady) {
      uint16_t co2 = 0;
      float temperature = 0.0f;
      float humidity = 0.0f;
      error = scd40.readMeasurement(co2, temperature, humidity);
      if (!error) {
        float rawHumidity = humidity;
        humidity = constrain(humidity + HUM_OFFSET_PCT, 0.0f, 100.0f);
        Serial.printf("CO2: %u ppm | T: %.1f°C | RH: %.1f%% (brut %.1f%%)\r\n", co2, temperature, humidity, rawHumidity);
        zbTempHum.setTemperature(temperature);
        zbTempHum.setHumidity(humidity);
        zbCO2.setCarbonDioxide((float)co2);
      } else {
        errorToString(error, errorMessage, sizeof(errorMessage));
        Serial.printf("Erreur lecture SCD40: %s\r\n", errorMessage);
      }
    }
  }

  // Envoi forcé toutes les 30 s des dernières valeurs mesurées
  if (Zigbee.connected() && millis() - lastReport >= REPORT_INTERVAL_MS) {
    lastReport = millis();
    bool ok = zbTempHum.report();   // température + humidité
    ok = zbCO2.report() && ok;      // CO2
    Serial.println(ok ? "Rapport Zigbee envoyé" : "Échec d'envoi du rapport Zigbee");
  }
}
