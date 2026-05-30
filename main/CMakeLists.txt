idf_component_register(
    SRCS
    "main.c"
    "zigbee.c"
    "co2_task.c"
    "zigbee_task.c"
    INCLUDE_DIRS "."
    REQUIRES "esp-idf-scd4x" "nvs_flash" "espressif__esp-zigbee-lib" "esp_timer" "app_update"
)
