#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_zigbee_ota.h"
#include "esp_ota_ops.h"
#include "zigbee.h"
#include "tasks.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "esp_sleep.h"
#include "config.h"

#define DEFINE_PSTRING(var, str)   \
    const struct                   \
    {                              \
        unsigned char len;         \
        char content[sizeof(str)]; \
    }(var) = {sizeof(str) - 1, (str)}


static const char *TAG = "zigbee_task";

volatile bool ota_in_progress = false;

// ---------------------- OTA upgrade handler ----------------------

static const esp_partition_t *ota_partition = NULL;
static esp_ota_handle_t ota_handle = 0;

static esp_err_t zb_ota_upgrade_status_handler(esp_zb_zcl_ota_upgrade_value_message_t message)
{
    static uint32_t total_size = 0;
    static uint32_t offset = 0;
    esp_err_t ret = ESP_OK;

    if (message.info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "OTA: error status(%d)", message.info.status);
        return ESP_FAIL;
    }

    switch (message.upgrade_status) {
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
        ESP_LOGI(TAG, "OTA: Start (manufacturer: 0x%x, type: 0x%x, version: 0x%lx, size: %ld)",
                 message.ota_header.manufacturer_code,
                 message.ota_header.image_type,
                 message.ota_header.file_version,
                 message.ota_header.image_size);
        ota_partition = esp_ota_get_next_update_partition(NULL);
        if (!ota_partition) {
            ESP_LOGE(TAG, "OTA: No update partition found");
            return ESP_FAIL;
        }
        ret = esp_ota_begin(ota_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA: esp_ota_begin failed (%s)", esp_err_to_name(ret));
            return ret;
        }
        total_size = message.ota_header.image_size;
        offset = 0;
        ota_in_progress = true;
        ESP_LOGI(TAG, "OTA: Writing to partition '%s'", ota_partition->label);
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
        ret = esp_ota_write(ota_handle, message.payload, message.payload_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA: esp_ota_write failed (%s)", esp_err_to_name(ret));
            return ret;
        }
        offset += message.payload_size;
        if (total_size > 0) {
            uint32_t pct = offset * 100 / total_size;
            if (pct % 10 == 0 && pct != ((offset - message.payload_size) * 100 / total_size)) {
                ESP_LOGI(TAG, "OTA: %ld%% (%ld/%ld)", pct, offset, total_size);
            }
        }
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
        ESP_LOGI(TAG, "OTA: Verifying image...");
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
        ret = esp_ota_end(ota_handle);
        ota_handle = 0;
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA: esp_ota_end failed (%s)", esp_err_to_name(ret));
            ota_in_progress = false;
            return ret;
        }
        ESP_LOGI(TAG, "OTA: Image verified OK");
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
        ESP_LOGI(TAG, "OTA: Complete - setting boot partition and rebooting");
        ret = esp_ota_set_boot_partition(ota_partition);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA: esp_ota_set_boot_partition failed (%s)", esp_err_to_name(ret));
            ota_in_progress = false;
            return ret;
        }
        ESP_LOGI(TAG, "OTA: Rebooting in 2s...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        esp_restart();
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT:
        ESP_LOGW(TAG, "OTA: Aborted");
        if (ota_handle) {
            esp_ota_abort(ota_handle);
            ota_handle = 0;
        }
        ota_in_progress = false;
        offset = 0;
        total_size = 0;
        break;

    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR:
        ESP_LOGE(TAG, "OTA: Error");
        if (ota_handle) {
            esp_ota_abort(ota_handle);
            ota_handle = 0;
        }
        ota_in_progress = false;
        break;

    default:
        ESP_LOGD(TAG, "OTA: Status 0x%x", message.upgrade_status);
        break;
    }
    return ret;
}

// ---------------------- Attribute handler ----------------------

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);
    ESP_LOGI(TAG, "Received message: endpoint(0x%x), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);
    return ret;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id)
    {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
        ret = zb_ota_upgrade_status_handler(*(esp_zb_zcl_ota_upgrade_value_message_t *)message);
        break;
    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}

/* initialize Zigbee stack with Zigbee end-device config */
void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack with Zigbee end-device config */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    /* Enable zigbee light sleep */
    esp_zb_sleep_enable(true);
    esp_zb_sleep_set_threshold(ZIGBEE_SLEEP_THRESHOLD);
    // zb_zdo_pim_set_long_poll_interval(ED_KEEP_ALIVE);
    esp_zb_init(&zb_nwk_cfg);

    // ------------------------------ Cluster BASIC ------------------------------
    esp_zb_basic_cluster_cfg_t basic_cluster_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x04, // mains powered
    };
    uint32_t ApplicationVersion = OTA_UPGRADE_FILE_VERSION;
    uint32_t StackVersion = 0x0002;
    uint32_t HWVersion = OTA_UPGRADE_HW_VERSION;
    DEFINE_PSTRING(ManufacturerName, "FlorianL");
    DEFINE_PSTRING(ModelIdentifier, "CO2 Sensor");
    DEFINE_PSTRING(DateCode, "20260122");

    esp_zb_attribute_list_t *esp_zb_basic_cluster = esp_zb_basic_cluster_create(&basic_cluster_cfg);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, &ApplicationVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_STACK_VERSION_ID, &StackVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_HW_VERSION_ID, &HWVersion);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)&ManufacturerName);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)&ModelIdentifier);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, (void *)&DateCode);

    // ------------------------------ Cluster IDENTIFY ------------------------------
    esp_zb_identify_cluster_cfg_t identify_cluster_cfg = {
        .identify_time = 0,
    };
    esp_zb_attribute_list_t *esp_zb_identify_cluster = esp_zb_identify_cluster_create(&identify_cluster_cfg);

    // ------------------------------ Cluster Temperature ------------------------------
    esp_zb_temperature_meas_cluster_cfg_t temperature_meas_cfg = {
        .measured_value = 0,
        .min_value = TEMP_MIN,
        .max_value = TEMP_MAX,
    };
    esp_zb_attribute_list_t *esp_zb_temperature_meas_cluster = esp_zb_temperature_meas_cluster_create(&temperature_meas_cfg);

    // ------------------------------ Cluster Humidity ------------------------------
    esp_zb_humidity_meas_cluster_cfg_t humidity_meas_cfg = {
        .measured_value = 0,
        .min_value = HUMID_MIN,
        .max_value = HUMID_MAX,
    };
    esp_zb_attribute_list_t *esp_zb_humidity_meas_cluster = esp_zb_humidity_meas_cluster_create(&humidity_meas_cfg);

    // ------------------------------ Cluster CO2 -----------------------------------
    esp_zb_carbon_dioxide_measurement_cluster_cfg_t carbon_dioxide_meas_cfg = {
        .measured_value = 0,
        .min_measured_value = CO2_MIN,
        .max_measured_value = CO2_MAX,
    };
    esp_zb_attribute_list_t *esp_zb_carbon_dioxide_meas_cluster = esp_zb_carbon_dioxide_measurement_cluster_create(&carbon_dioxide_meas_cfg);

    // ------------------------------ Cluster OTA Upgrade (Client) ------------------
    esp_zb_ota_cluster_cfg_t ota_cluster_cfg = {
        .ota_upgrade_file_version = OTA_UPGRADE_FILE_VERSION,
        .ota_upgrade_manufacturer = OTA_UPGRADE_MANUFACTURER,
        .ota_upgrade_image_type = OTA_UPGRADE_IMAGE_TYPE,
        .ota_min_block_reque = 0,
        .ota_upgrade_file_offset = ESP_ZB_ZCL_OTA_UPGRADE_FILE_OFFSET_DEF_VALUE,
        .ota_upgrade_downloaded_file_ver = ESP_ZB_ZCL_OTA_UPGRADE_DOWNLOADED_FILE_VERSION_DEF_VALUE,
        .ota_upgrade_server_id = ESP_ZB_ZCL_OTA_UPGRADE_SERVER_DEF_VALUE,
        .ota_image_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_IMAGE_STATUS_DEF_VALUE,
    };
    esp_zb_attribute_list_t *esp_zb_ota_client_cluster = esp_zb_ota_cluster_create(&ota_cluster_cfg);
    esp_zb_zcl_ota_upgrade_client_variable_t ota_client_var = {
        .timer_query = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,
        .hw_version = OTA_UPGRADE_HW_VERSION,
        .max_data_size = OTA_UPGRADE_MAX_DATA_SIZE,
    };
    esp_zb_ota_cluster_add_attr(esp_zb_ota_client_cluster,
        ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID, (void *)&ota_client_var);

    // ------------------------------ Create cluster list ------------------------------
    esp_zb_cluster_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(esp_zb_cluster_list, esp_zb_identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_temperature_meas_cluster(esp_zb_cluster_list, esp_zb_temperature_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_humidity_meas_cluster(esp_zb_cluster_list, esp_zb_humidity_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_carbon_dioxide_measurement_cluster(esp_zb_cluster_list, esp_zb_carbon_dioxide_meas_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_ota_cluster(esp_zb_cluster_list, esp_zb_ota_client_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

    // ------------------------------ Create endpoint list ------------------------------
    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = HA_ESP_CO2_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
    };

    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list, endpoint_config);

    // ------------------------------ Register Device ------------------------------
    esp_zb_device_register(esp_zb_ep_list);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}
