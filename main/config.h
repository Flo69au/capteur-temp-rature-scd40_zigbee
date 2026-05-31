#pragma once

#include <stdint.h>
#include "driver/gpio.h"


#define SDC4X_SDA_PIN GPIO_NUM_22
#define SDC4X_SCL_PIN  GPIO_NUM_23

// SCD4x I2C address (7-bit, fixed, datasheet §4.1)
#define SCD4X_I2C_ADDR 0x62

// SCD4x electrical specs at VDD = 3.3 V (datasheet §2.1)
//   Supply voltage       : 2.4 V – 5.5 V (ripple < 30 mV peak-to-peak)
//   Peak current         : 175 mA typ, 205 mA max
//   Avg current periodic (1 meas / 5 s)     : 15 mA typ, 18 mA max
//   Avg current low-power (1 meas / 30 s)   :  3.2 mA typ, 3.5 mA max
//   Avg current single shot (1 meas / 5 min, SCD41/43 only): 0.45 mA typ, 0.5 mA max

// SCD4x timing constants (datasheet §3.2 and §5)
#define SCD4X_POWERUP_TIME_MS        30   // Max time from VDD ≥ 2.25 V to idle
#define SCD4X_SOFT_RESET_TIME_MS     30   // Max time after reinit command
#define SCD4X_STOP_MEAS_WAIT_MS     500   // Must wait after stop_periodic_measurement
#define SCD4X_SINGLE_SHOT_TIME_MS  5000   // measure_single_shot (SCD41/43 only)
#define SCD4X_RHT_ONLY_TIME_MS       50   // measure_single_shot_rht_only (SCD41/43 only)
#define SCD4X_FRC_TIME_MS           400   // perform_forced_recalibration
#define SCD4X_SELF_TEST_TIME_MS   10000   // perform_self_test
#define SCD4X_PERSIST_TIME_MS       800   // persist_settings (EEPROM, max 2000 writes)
#define SCD4X_FACTORY_RESET_TIME_MS 1200  // perform_factory_reset

// Measurement interval (10 seconds)
#define MEASURE_INTERVAL_MS (10 * 1000)


// Sanity values for measurement bounds (SCD40 datasheet v1.7 – April 2025)
//
// CO2:
//   Range       : 0 – 40 000 ppm
//   Accuracy    : ±(50 ppm + 5 % of reading)  @ 400–2 000 ppm
//   Repeatability: ±10 ppm (typ.)
//   Response time: τ63 % = 60 s (typ., step change 400–2 000 ppm)
//   Drift       : ±(5 ppm + 0.5 % of reading) / year after 5 years
#define CO2_MIN 0.0
#define CO2_MAX 40000.0

// Humidity:
//   Range       : 0 – 100 %RH
//   Accuracy    : ±6 %RH  (typ., 15–35 °C, 20–65 %RH)
//                 ±9 %RH  (typ., -10–60 °C, 0–100 %RH)
//   Repeatability: ±0.4 %RH (typ.)
//   Response time: τ63 % = 90 s (typ., periodic measurement mode)
//   Drift       : <0.25 %RH / year
#define HUMID_MIN 0
#define HUMID_MAX 100

// Temperature:
//   Range       : -10 °C – 60 °C
//   Accuracy    : ±0.8 °C (typ., 15–35 °C)
//                 ±1.5 °C (typ., -10–60 °C)
//   Repeatability: ±0.1 °C (typ.)
//   Response time: τ63 % = 120 s (typ., periodic measurement mode)
//   Drift       : <0.03 °C / year
#define TEMP_MIN -10
#define TEMP_MAX 60

// Sensor calibration settings
#define SENSOR_ALTITUDE_METERS 530  // Saint-Just-la-Pendue, Loire (428-637m) — valid range: 0–3000 m (datasheet §6.6)
#define SENSOR_TEMP_OFFSET_MILLI_C 4072  // Temperature offset in milli-°C (calibrated 2026-03-30: avg diff ref - capteur = -2.51°C sur 153 pts → +2512 milli-°C ajoutés)
                                          // Factory default: 4000 milli-°C (4 °C). Range recommandée: 0–20 °C (datasheet §6.4)
// Si le SCD40 lit trop chaud → augmentez la valeur
// Si le SCD40 lit trop froid → diminuez la valeur
#define SENSOR_HUMIDITY_OFFSET_CENTI_PERCENT 422   // Humidity offset in centi-% (calibrated 2026-05-27: capteur -4.22% sous ref → +4.22% offset; ref=22.68%, capteur=18.46%, 170 pts)
// Si le SCD40 lit 55% mais il fait 52% → mettez -300 (-3%)
// Si le SCD40 lit 45% mais il fait 48% → mettez 300 (+3%)

// Reporting thresholds (send on change)
#define TEMP_CHANGE_THRESHOLD 10      // 0.1°C (in centidegrees, 2 decimal places)
#define HUMID_CHANGE_THRESHOLD 100    // 1% (in centipercent)
#define CO2_CHANGE_THRESHOLD 10       // 10 ppm

// Force report if no change for this duration (30 minutes)
#define FORCE_REPORT_INTERVAL_MS (30 * 60 * 1000)

// OTA firmware update settings
#define OTA_UPGRADE_MANUFACTURER    0x131B       // Espressif manufacturer code
#define OTA_UPGRADE_IMAGE_TYPE      0x1011       // Unique image type for this product
#define OTA_UPGRADE_FILE_VERSION    0x00050200   // v5.2 — format: 0xMMmmPP00 (major.minor.patch)
#define OTA_UPGRADE_HW_VERSION      0x0002
#define OTA_UPGRADE_MAX_DATA_SIZE   64

// Misc settings, for very fine tuning
// time in ms before the zigbee stack sends a CAN_SLEEP signal
#define ZIGBEE_SLEEP_THRESHOLD 20

// LED GPIO15 — clignote pendant l'appairage, fixe quand connecté
#define PAIRING_LED_GPIO       GPIO_NUM_15
#define PAIRING_LED_ACTIVE     0            // 0 = active LOW (LED allumée quand GPIO=0)
