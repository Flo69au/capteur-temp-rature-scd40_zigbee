#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "driver/gpio.h"
#include "zigbee.h"
#include "tasks.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "esp_sleep.h"
#include "config.h"

static const char *TAG = "CO2";

// Bouton BOOT du XIAO ESP32-C6 (GPIO9) — maintenir 5s au démarrage pour réappairage
#define FACTORY_RESET_BUTTON_GPIO  GPIO_NUM_9
#define FACTORY_RESET_HOLD_MS      5000

// ---------------------- LED appairage ----------------------

static TaskHandle_t led_blink_task_handle = NULL;

static void led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PAIRING_LED_GPIO) | (1ULL << CONNECTED_LED_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PAIRING_LED_GPIO,   !PAIRING_LED_ACTIVE);
    gpio_set_level(CONNECTED_LED_GPIO, !CONNECTED_LED_ACTIVE);
}

static void led_set(bool on)
{
    gpio_set_level(PAIRING_LED_GPIO, on ? PAIRING_LED_ACTIVE : !PAIRING_LED_ACTIVE);
}

static void connected_led_set(bool on)
{
    gpio_set_level(CONNECTED_LED_GPIO, on ? CONNECTED_LED_ACTIVE : !CONNECTED_LED_ACTIVE);
}

static void led_blink_task(void *period_ms_ptr)
{
    uint32_t period_ms = (uint32_t)(uintptr_t)period_ms_ptr;
    while (1) {
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(period_ms / 2));
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(period_ms / 2));
    }
}

void pairing_led_start(void)
{
    if (led_blink_task_handle != NULL) return;
    xTaskCreate(led_blink_task, "led_blink", 1024,
                (void *)(uintptr_t)500, 3, &led_blink_task_handle);
}

void pairing_led_stop(void)
{
    if (led_blink_task_handle == NULL) return;
    vTaskDelete(led_blink_task_handle);
    led_blink_task_handle = NULL;
    led_set(false);
}

// ---------------------- Factory reset ----------------------

static void check_factory_reset(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FACTORY_RESET_BUTTON_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    if (gpio_get_level(FACTORY_RESET_BUTTON_GPIO) != 0) {
        return;
    }

    ESP_LOGW(TAG, "Bouton BOOT pressé — maintenez 5s pour réinitialiser Zigbee...");

    int held_ms = 0;
    bool led_state = false;
    while (gpio_get_level(FACTORY_RESET_BUTTON_GPIO) == 0 && held_ms < FACTORY_RESET_HOLD_MS) {
        led_state = !led_state;
        led_set(led_state); // clignotement rapide pendant l'appui
        vTaskDelay(pdMS_TO_TICKS(200));
        held_ms += 200;
    }
    led_set(false);

    if (held_ms < FACTORY_RESET_HOLD_MS) {
        ESP_LOGI(TAG, "Appui relâché — démarrage normal");
        return;
    }

    // Clignotement très rapide = confirmation reset
    for (int i = 0; i < 10; i++) {
        led_set(i % 2 == 0);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    led_set(false);

    ESP_LOGW(TAG, "Réinitialisation Zigbee: effacement des partitions...");
    const char *partitions[] = {"zb_storage", "zb_fct"};
    for (int i = 0; i < 2; i++) {
        const esp_partition_t *p = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partitions[i]);
        if (p) {
            esp_partition_erase_range(p, 0, p->size);
            ESP_LOGW(TAG, "Partition '%s' effacée", partitions[i]);
        }
    }
    ESP_LOGW(TAG, "Prêt pour l'appairage — redémarrage...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

TaskHandle_t xSdc41Task;
TaskHandle_t xZigbeeTask;

// Turn on to get debug output for diagnosing issues with system sleeping behaviour
#define DEBUG_SLEEP 0


static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee bdb commissioning");
}

void start_sensor_measurements()
{
    if (xSdc41Task == NULL) {
        xTaskCreate(sdc41_task, "sdc41_task", 4096, NULL, 5, &xSdc41Task);
    }
}

static void confirm_ota_rollback(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "OTA: Confirming new firmware is valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    static bool allow_sleep = false;
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    uint32_t sensor_state = 0;
    switch (sig_type)
    {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK)
        {
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                pairing_led_start();
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted");
                confirm_ota_rollback();
                start_sensor_measurements();
            }
        }
        else
        {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s). Resetting the esp after 2s", esp_err_to_name(err_status));
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            esp_restart();
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK)
        {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
            pairing_led_stop();
            connected_led_set(true);
            zb_zdo_pim_set_long_poll_interval(ED_KEEP_ALIVE);
            confirm_ota_rollback();
            start_sensor_measurements();
        }
        else
        {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    case ESP_ZB_ZDO_DEVICE_UNAVAILABLE:
        ESP_LOGW(TAG, "Parent router unavailable - attempting network rejoin");
        connected_led_set(false);
        pairing_led_start();
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        break;
    case ESP_ZB_NLME_STATUS_INDICATION:
        ESP_LOGW(TAG, "NLME status indication (status: %s)", esp_err_to_name(err_status));
        break;
    case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
        BaseType_t xResult = xTaskNotifyWait( pdFALSE,          /* Don't clear bits on entry. */
                                 ULONG_MAX,        /* Clear all bits on exit. */
                                 &sensor_state,
                                 0 ); /* do not wait for the flag to be set, simply skip going to sleep in this case */
        if( xResult == pdPASS )
        {
            switch(sensor_state) {
                case CO2_MEASUREMENT_PENDING:
                    allow_sleep = false;
                    break;
                case CO2_MEASUREMENT_DONE:
                    allow_sleep = true;
                    break;
            }
        }
        if(allow_sleep && !ota_in_progress) {
            // sensor values have been read and sending via zigbee was requested
            #if DEBUG_SLEEP
            ESP_LOGI(TAG, "Going to sleep");
            #endif
            esp_zb_sleep_now();
            #if DEBUG_SLEEP
            esp_sleep_source_t wake_up_cause = esp_sleep_get_wakeup_cause();
            ESP_LOGI(TAG, "Woke up because: %d", wake_up_cause);
            #endif
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

#if CONFIG_PM_ENABLE
static esp_err_t esp_zb_power_save_init(void)
{
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 40,
        .light_sleep_enable = true
    };
    return esp_pm_configure(&pm_config);
}
#endif

void configure_internal_antenna(void) {
    gpio_reset_pin(GPIO_NUM_3);
    gpio_reset_pin(GPIO_NUM_14);
    gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_3, 0);//turn on antenna selection
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_14, 0);//use internal antenna
}
#if DEBUG_SLEEP
void pm_dump(void *pvParameters) {
    while (1) {
        esp_pm_dump_locks(stdout);
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}
#endif

void app_main(void)
{
    led_init();
    check_factory_reset();
    configure_internal_antenna();
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
#if CONFIG_PM_ENABLE
    /* enable power saving */
    ESP_ERROR_CHECK(esp_zb_power_save_init());
#endif
    // zb_deep_sleep_init();
    /* load Zigbee light_bulb platform config to initialization */
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    /* hardware related and device init */
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, &xZigbeeTask);
    #if DEBUG_SLEEP
    xTaskCreate(pm_dump, "pm_dump", 4096, NULL, 5, NULL);
    #endif
}
