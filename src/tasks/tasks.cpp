#include "tasks.h"
#include "config.h"
#include "globals.h"

void createAllTasks() {
    xTaskCreatePinnedToCore(
        gpsTask,
        "GPS Task",
        GPS_TASK_STACK_SIZE,
        NULL,
        GPS_TASK_PRIORITY,
        &gpsTaskHandle,
        1
    );

    xTaskCreatePinnedToCore(
        meshtasticTask,
        "Meshtastic Task",
        MESHTASTIC_TASK_STACK_SIZE,
        NULL,
        MESHTASTIC_TASK_PRIORITY,
        &meshtasticTaskHandle,
        1
    );

    xTaskCreatePinnedToCore(
        meshtasticCallbackTask,
        "Meshtastic Callback Task",
        MESHTASTIC_CALLBACK_TASK_STACK_SIZE,
        NULL,
        MESHTASTIC_CALLBACK_TASK_PRIORITY,
        &meshtasticCallbackTaskHandle,
        1
    );

    xTaskCreatePinnedToCore(
        eepromTask,
        "EEPROM Task",
        EEPROM_TASK_STACK_SIZE,
        NULL,
        EEPROM_TASK_PRIORITY,
        &eepromTaskHandle,
        1
    );

    xTaskCreatePinnedToCore(
        systemTask,
        "System Task",
        SYSTEM_TASK_STACK_SIZE,
        NULL,
        SYSTEM_TASK_PRIORITY,
        &systemTaskHandle,
        1
    );

    // Core 0: espnow_task and ble_task share radio hardware with WiFi.
    xTaskCreatePinnedToCore(
        espnowTask,
        "ESP-NOW Task",
        ESPNOW_TASK_STACK_SIZE,
        NULL,
        ESPNOW_TASK_PRIORITY,
        &espnowTaskHandle,
        0  // Core 0 for WiFi operations
    );

    xTaskCreatePinnedToCore(
        bleTask,
        "BLE Task",
        BLE_TASK_STACK_SIZE,
        NULL,
        BLE_TASK_PRIORITY,
        &bleTaskHandle,
        0
    );

#if DEBUG_INIT == 1
    Serial.println("All FreeRTOS tasks created");
#endif
}