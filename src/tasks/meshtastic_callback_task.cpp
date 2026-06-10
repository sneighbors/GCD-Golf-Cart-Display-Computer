#include "meshtastic_callback_task.h"
#include "config.h"
#include "globals.h"
#include "types.h"
#include "communication/hot_packet_parser.h"
#include "communication/chat_buffer.h"
#include "hardware/display.h"
#include "tasks/ble_task.h"
#include "Meshtastic.h"
#include <time.h>

void meshtasticCallbackTask(void *parameter) {
    meshtasticCallbackItem_t item;

    while (true) {
        if (xQueueReceive(meshtasticCallbackQueue, &item, portMAX_DELAY)) {
#if DEBUG_GCM_MESSAGES
            Serial.printf("GCM RX: ch=%d from=%lu to=%lu msg='%s'\n",
                         item.channel, item.from, item.to, item.text);
            if (item.to == 0xFFFFFFFF) {
                Serial.println("  (BROADCAST)");
            } else if (item.to == my_node_num) {
                Serial.println("  (DM to me)");
            } else {
                Serial.printf("  (DM to someone else; my_node_num=%lu)\n", my_node_num);
            }
#endif

            // Append every text message to the chat ring buffer.
            // chatAbbreviate() turns HoT/GC packets into short tags
            // before storage; processHotPacket below still sees the
            // original full text for its structured-data side effects.
            chatMessage_t cm = {};
            cm.from      = item.from;
            cm.to        = item.to;
            cm.channel   = item.channel;
            cm.timestamp = item.timestamp ? item.timestamp : (uint32_t)time(NULL);
            cm.outgoing  = false;
            strncpy(cm.text, item.text, sizeof(cm.text) - 1);
            cm.text[sizeof(cm.text) - 1] = '\0';
            chatBufferAppend(&cm);
            blePushChat(&cm);

            if (item.to == my_node_num && my_node_num != 0) {
                tone_message();
                blePushAlert(BLE_ALERT_NEW_DM, "");
                eepromWriteItem_t nvsItem = {};
                nvsItem.type = EEPROM_SAVE_DMS;
                xQueueSend(eepromWriteQueue, &nvsItem, 0);
            } else {
                blePushAlert(BLE_ALERT_NEW_BROADCAST, "");
            }

            // Hot packet structured-data parser still gets the full original text.
            if (isHotPacket(item.text)) {
                processHotPacket(item.text);
            }
        }
    }
}

void connected_callback(mt_node_t *node, mt_nr_progress_t progress) {
    if (not_yet_connected) {
        Serial.println("Connected to Meshtastic device!");
        not_yet_connected = false;
    }
}

void text_message_callback(uint32_t from, uint32_t to, uint8_t channel, const char *text) {
#if DEBUG_GCM_MESSAGES
    Serial.printf("GCM RX callback: from=%lu, to=%lu, ch=%d, text='%s'\n",
                 from, to, channel, text ? text : "NULL");
#endif

    meshtasticCallbackItem_t item;
    item.from = from;
    item.to = to;
    item.channel = channel;
    item.timestamp = (uint32_t)time(NULL);

    if (text != NULL) {
        strncpy(item.text, text, MAX_MESHTASTIC_PAYLOAD - 1);
        item.text[MAX_MESHTASTIC_PAYLOAD - 1] = '\0';
    } else {
        item.text[0] = '\0';
    }
    
    if (xQueueSend(meshtasticCallbackQueue, &item, 0) != pdTRUE) {
        Serial.println("Warning: Meshtastic callback queue full, message dropped");
    }
}