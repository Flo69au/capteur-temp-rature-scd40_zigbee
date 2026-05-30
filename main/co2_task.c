#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "zigbee.h"
#include "scd4x_i2c.h"
#include "sensirion_i2c_hal.h"
#include "config.h"
#include "tasks.h"
#include <stdlib.h>


static const char *TAG = "scd40_task";

// Envoie jusqu'à 9 pulses SCL pour libérer un SDA bloqué bas (I2C spec §3.1.16)
static void i2c_recover(void)
{
    gpio_set_direction(SDC4X_SCL_PIN, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction(SDC4X_SDA_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SDC4X_SCL_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SDC4X_SDA_PIN, GPIO_PULLUP_ONLY);
    gpio_set_level(SDC4X_SCL_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    if (gpio_get_level(SDC4X_SDA_PIN) == 1) {
        return; // bus libre, rien à faire
    }

    ESP_LOGW(TAG, "I2C: SDA bloqué bas, récupération en cours...");
    for (int i = 0; i < 9; i++) {
        gpio_set_level(SDC4X_SCL_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(SDC4X_SCL_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        if (gpio_get_level(SDC4X_SDA_PIN) == 1) {
            ESP_LOGI(TAG, "I2C: SDA libéré après %d pulses", i + 1);
            break;
        }
    }

    // Condition STOP pour terminer proprement la transaction
    gpio_set_direction(SDC4X_SDA_PIN, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(SDC4X_SDA_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(SDC4X_SCL_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(SDC4X_SDA_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    if (gpio_get_level(SDC4X_SDA_PIN) == 0) {
        ESP_LOGE(TAG, "I2C: récupération échouée, SDA toujours bas — vérifiez le câblage");
    }
}

RTC_DATA_ATTR bool scd4x_initialized = false;

// Last reported values (stored in RTC memory to survive deep sleep)
RTC_DATA_ATTR int16_t last_reported_temp = INT16_MIN;
RTC_DATA_ATTR int16_t last_reported_humid = INT16_MIN;
RTC_DATA_ATTR uint16_t last_reported_co2 = UINT16_MAX;

// Last report timestamps (in milliseconds)
RTC_DATA_ATTR int64_t last_temp_report_time = 0;
RTC_DATA_ATTR int64_t last_humid_report_time = 0;
RTC_DATA_ATTR int64_t last_co2_report_time = 0;

void sdc41_task(void *pvParameters)
{
    int16_t error = 0;
    i2c_recover();
    sensirion_i2c_hal_init(SDC4X_SDA_PIN, SDC4X_SCL_PIN);
    if (!scd4x_initialized) {
        // Clean up potential SCD40 states (scd4x_wake_up is SCD41/43 only — not called here)
        ESP_LOGI(TAG, "First boot after power cycle. Resetting SCD40 sensor state, some i2c errors are expected");
        scd4x_stop_periodic_measurement();
        vTaskDelay(SCD4X_STOP_MEAS_WAIT_MS / portTICK_PERIOD_MS);
        scd4x_reinit();
        vTaskDelay(SCD4X_SOFT_RESET_TIME_MS / portTICK_PERIOD_MS);

        uint16_t serial_0;
        uint16_t serial_1;
        uint16_t serial_2;
        error = scd4x_get_serial_number(&serial_0, &serial_1, &serial_2);
        if (error) {
            ESP_LOGE(TAG, "Error executing scd4x_get_serial_number(): %i", error);
        } else {
            ESP_LOGI(TAG, "Serial: 0x%04x%04x%04x", serial_0, serial_1, serial_2);
        }

        // Check calibration status
        uint16_t asc_enabled;
        error = scd4x_get_automatic_self_calibration(&asc_enabled);
        if (error) {
            ESP_LOGE(TAG, "Error reading ASC status: %i", error);
        } else {
            ESP_LOGI(TAG, "Automatic Self-Calibration (ASC): %s", asc_enabled ? "ENABLED" : "DISABLED");
        }

        // Configure temperature offset
        error = scd4x_set_temperature_offset(SENSOR_TEMP_OFFSET_MILLI_C);
        if (error) {
            ESP_LOGE(TAG, "Error setting temperature offset: %i", error);
        } else {
            ESP_LOGI(TAG, "Temperature offset configured to: %.2f °C", SENSOR_TEMP_OFFSET_MILLI_C / 1000.0f);
        }

        // Verify temperature offset
        int32_t t_offset;
        error = scd4x_get_temperature_offset(&t_offset);
        if (error) {
            ESP_LOGE(TAG, "Error reading temperature offset: %i", error);
        } else {
            ESP_LOGI(TAG, "Temperature offset verified: %.2f °C", t_offset / 1000.0f);
        }

        // Configure and verify sensor altitude
        error = scd4x_set_sensor_altitude(SENSOR_ALTITUDE_METERS);
        if (error) {
            ESP_LOGE(TAG, "Error setting sensor altitude: %i", error);
        } else {
            ESP_LOGI(TAG, "Sensor altitude configured to: %d m", SENSOR_ALTITUDE_METERS);
        }

        uint16_t altitude;
        error = scd4x_get_sensor_altitude(&altitude);
        if (error) {
            ESP_LOGE(TAG, "Error reading sensor altitude: %i", error);
        } else {
            ESP_LOGI(TAG, "Sensor altitude verified: %d m", altitude);
        }

        // Perform self-test
        uint16_t sensor_status;
        ESP_LOGI(TAG, "Performing self-test (takes ~10 seconds)...");
        error = scd4x_perform_self_test(&sensor_status);
        if (error) {
            ESP_LOGE(TAG, "Error performing self-test: %i", error);
        } else {
            if (sensor_status == 0) {
                ESP_LOGI(TAG, "Self-test: PASSED - No malfunction detected");
            } else {
                ESP_LOGW(TAG, "Self-test: FAILED - Malfunction detected (status: 0x%04x)", sensor_status);
            }
        }

        // Start periodic measurement — SCD40 produces a new sample every 5 seconds
        error = scd4x_start_periodic_measurement();
        if (error) {
            ESP_LOGE(TAG, "Error starting periodic measurement: %i", error);
        } else {
            ESP_LOGI(TAG, "Periodic measurement started (5 s update interval)");
        }

        scd4x_initialized = true;
    } else {
        // After deep sleep the sensor may have lost state; restart periodic measurement
        ESP_LOGI(TAG, "Wakeup from deep sleep. Restarting SCD40 periodic measurement");
        error = scd4x_start_periodic_measurement();
        if (error) {
            ESP_LOGE(TAG, "Error restarting periodic measurement: %i", error);
        }
    }

    uint16_t co2;
    int32_t temperature;
    int32_t humidity;
    int16_t zb_temperature;
    int16_t zb_humidity;
    float_t zb_co2;
    float_t sanity_temperature;
    float_t sanity_humidity;
    bool plausible;
    bool data_ready_flag;
    int consecutive_errors = 0;

    while (1)
    {
        // Wait for the next measurement window.
        // The SCD40 updates every 5 s in periodic mode; MEASURE_INTERVAL_MS (10 s) is
        // always longer, so data will be ready when we poll.
        vTaskDelay(MEASURE_INTERVAL_MS / portTICK_PERIOD_MS);

        // Confirm data is available before reading
        data_ready_flag = false;
        error = scd4x_get_data_ready_flag(&data_ready_flag);
        if (error) {
            ESP_LOGE(TAG, "Error executing scd4x_get_data_ready_flag(): %i", error);
            if (++consecutive_errors >= 3) {
                ESP_LOGW(TAG, "3 erreurs I2C consécutives, tentative de récupération...");
                i2c_recover();                          // libère SDA si bloqué bas (GPIO)
                sensirion_i2c_hal_free();               // détruit le bus proprement
                vTaskDelay(pdMS_TO_TICKS(10));
                sensirion_i2c_hal_init(SDC4X_SDA_PIN, SDC4X_SCL_PIN);  // réinitialise
                scd4x_stop_periodic_measurement();
                vTaskDelay(pdMS_TO_TICKS(SCD4X_STOP_MEAS_WAIT_MS));
                scd4x_start_periodic_measurement();
                consecutive_errors = 0;
                scd4x_initialized = false;
            }
            continue;
        }
        consecutive_errors = 0;
        if (!data_ready_flag) {
            ESP_LOGW(TAG, "Data not ready yet, skipping this cycle");
            continue;
        }

        xTaskNotify(xZigbeeTask, CO2_MEASUREMENT_PENDING, eSetValueWithOverwrite);

        error = scd4x_read_measurement(&co2, &temperature, &humidity);
        if (error) {
            ESP_LOGE(TAG, "Error executing scd4x_read_measurement(): %i", error);
            continue;
        }

        plausible = true;
        zb_temperature = temperature / 10;
        zb_humidity = humidity / 10;
        zb_co2 = (float_t)co2 / 1000000.0f;
        sanity_temperature = zb_temperature / 100.0;
        sanity_humidity = zb_humidity / 100.0;
        ESP_LOGI(TAG, "Hum: %.1f %%; Tmp: %.2f °C; CO2: %d ppm", sanity_humidity, sanity_temperature, co2);

        int64_t current_time = esp_timer_get_time() / 1000; // Convert to ms

        // Temperature: report if change >= 0.1°C or 30min timeout
        if (sanity_temperature >= TEMP_MIN && sanity_temperature <= TEMP_MAX) {
            bool temp_changed = (last_reported_temp == INT16_MIN) ||
                                (abs(zb_temperature - last_reported_temp) >= TEMP_CHANGE_THRESHOLD);
            bool temp_timeout = (current_time - last_temp_report_time) >= FORCE_REPORT_INTERVAL_MS;

            if (temp_changed || temp_timeout) {
                ESP_LOGI(TAG, "Reporting temperature: %.2f °C (changed: %d, timeout: %d)",
                         sanity_temperature, temp_changed, temp_timeout);
                reportAttribute(HA_ESP_CO2_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
                                ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &zb_temperature, 2);
                last_reported_temp = zb_temperature;
                last_temp_report_time = current_time;
            }
        } else {
            ESP_LOGW(TAG, "Temperature value is implausible and no Zigbee update will be sent");
            plausible = false;
        }

        // Humidity: report if change >= 1% or 30min timeout
        // Apply software offset correction
        int16_t zb_humidity_corrected = zb_humidity + SENSOR_HUMIDITY_OFFSET_CENTI_PERCENT;
        float_t sanity_humidity_corrected = zb_humidity_corrected / 100.0f;

        if (sanity_humidity_corrected >= HUMID_MIN && sanity_humidity_corrected <= HUMID_MAX) {
            bool humid_changed = (last_reported_humid == INT16_MIN) ||
                                 (abs(zb_humidity_corrected - last_reported_humid) >= HUMID_CHANGE_THRESHOLD);
            bool humid_timeout = (current_time - last_humid_report_time) >= FORCE_REPORT_INTERVAL_MS;

            if (humid_changed || humid_timeout) {
                ESP_LOGI(TAG, "Reporting humidity: %.1f %% (raw: %.1f %%, offset: %.2f %%) (changed: %d, timeout: %d)",
                         sanity_humidity_corrected, sanity_humidity, SENSOR_HUMIDITY_OFFSET_CENTI_PERCENT / 100.0f,
                         humid_changed, humid_timeout);
                reportAttribute(HA_ESP_CO2_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
                                ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &zb_humidity_corrected, 2);
                last_reported_humid = zb_humidity_corrected;
                last_humid_report_time = current_time;
            }
        } else {
            ESP_LOGW(TAG, "Humidity value is implausible and no Zigbee update will be sent");
            plausible = false;
        }

        // CO2: report if change >= 10 ppm or 30min timeout
        if (co2 >= CO2_MIN && co2 <= CO2_MAX) {
            bool co2_changed = (last_reported_co2 == UINT16_MAX) ||
                               (abs((int)co2 - (int)last_reported_co2) >= CO2_CHANGE_THRESHOLD);
            bool co2_timeout = (current_time - last_co2_report_time) >= FORCE_REPORT_INTERVAL_MS;

            if (co2_changed || co2_timeout) {
                ESP_LOGI(TAG, "Reporting CO2: %d ppm (changed: %d, timeout: %d)",
                         co2, co2_changed, co2_timeout);
                reportAttribute(HA_ESP_CO2_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT,
                                ESP_ZB_ZCL_ATTR_CARBON_DIOXIDE_MEASUREMENT_MEASURED_VALUE_ID, &zb_co2, 4);
                last_reported_co2 = co2;
                last_co2_report_time = current_time;
            }
        } else {
            ESP_LOGW(TAG, "CO2 value is implausible and no Zigbee update will be sent");
            plausible = false;
        }

        if (plausible) {
            xTaskNotify(xZigbeeTask, CO2_MEASUREMENT_DONE, eSetValueWithOverwrite);
        }
    }
}
