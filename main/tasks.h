#pragma once

typedef enum {
    CO2_MEASUREMENT_PENDING,
    CO2_MEASUREMENT_DONE
} SensorState;

// globally tracked task handles
extern TaskHandle_t xSdc41Task;
extern TaskHandle_t xZigbeeTask;

// OTA state flag (prevents sleep during OTA download)
extern volatile bool ota_in_progress;

void sdc41_task(void *pvParameters);
void esp_zb_task(void *pvParameters);
void pairing_led_start(void);
void pairing_led_stop(void);