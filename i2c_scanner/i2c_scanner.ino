/*
 * Scanner I2C - pour vérifier que le SCD40 est bien détecté
 * sur D4 (SDA) / D5 (SCL) du XIAO ESP32-C6, indépendamment du reste
 * (Zigbee, etc.).
 *
 * Pas besoin de réglages Zigbee particuliers pour ce test : Board =
 * XIAO_ESP32C6 suffit, les autres réglages Outils n'ont pas d'importance ici.
 *
 * Le SCD40 doit apparaître à l'adresse 0x62.
 */

#include <Wire.h>

#define I2C_SDA D4
#define I2C_SCL D5

void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("Scan I2C en cours...");
}

void loop() {
  int nbDevices = 0;

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("Device trouve a l'adresse 0x%02X", address);
      if (address == 0x62) {
        Serial.print("  <-- c'est le SCD40 !");
      }
      Serial.println();
      nbDevices++;
    } else if (error == 4) {
      Serial.printf("Erreur inconnue a l'adresse 0x%02X\r\n", address);
    }
  }

  if (nbDevices == 0) {
    Serial.println("Aucun device I2C trouve. Verifier alimentation (3V3/GND) et cablage SDA/SCL.");
  }

  Serial.println("---");
  delay(3000);
}
