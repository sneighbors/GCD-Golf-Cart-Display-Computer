#include "meshtastic_task.h"
#include "config.h"
#include "globals.h"
#include "Meshtastic.h"
#include "communication/meshtastic_admin.h"
#include "get_set_vars.h"
#include "types.h"
#include "prototypes.h"

void meshtasticTask(void *parameter) {
    static bool old_reboot_meshtastic = false;

    while (true) {
        uint32_t now = millis();

          // Handle mesh serial enable/disable
          if (mesh_serial_enabled != old_mesh_serial_enabled) {
              old_mesh_serial_enabled = mesh_serial_enabled;
              if (mesh_serial_enabled == false) {
                  mt_serial_end();
                  Serial.println("Meshtastic serial disabled");
              } else {
                  mt_serial_init(MT_SERIAL_RX_PIN, MT_SERIAL_TX_PIN, MT_DEV_BAUD_RATE);
                  Serial.println("Meshtastic serial enabled");
              }
          }

          // Handle Meshtastic reboot trigger
          if (reboot_meshtastic && !old_reboot_meshtastic) {
              Serial.println("\n=== Meshtastic Reboot Triggered ===");
              if (mesh_serial_enabled) {
                  if (mt_send_admin_reboot(0)) {  // 0 = immediate reboot
#if DEBUG_GCM_MESSAGES
                      Serial.println("GCM TX: Reboot command sent");
#endif
                  } else {
                      Serial.println("Failed to send reboot command");
                  }
              } else {
                  Serial.println("Cannot reboot: Meshtastic serial is disabled");
              }
              // Reset the trigger
              reboot_meshtastic = false;
          }
          old_reboot_meshtastic = reboot_meshtastic;

          // Call mt_loop if mesh_serial_enabled is enabled
          bool can_send = false;
          if (mesh_serial_enabled) {
              can_send = mt_loop(now);
          }

          // Retry connection if GCM was already running when GCD booted, or if GCM rebooted mid-session.
          // After initial GPS config (gpsConfigAttempted=true) we had a prior connection, so use the
          // lightweight SPECIAL_NONCE request (skips node database). Before that, use the full node
          // report to populate the short-name cache for favorites on first connect.
          static uint32_t lastNodeReportRetry = 0;
          if (can_send && not_yet_connected && (now - lastNodeReportRetry >= 5000)) {
              if (gpsConfigAttempted) {
                  mt_request_my_node_info();  // Post-reboot: lightweight, skips node database
              } else {
                  mt_request_node_report(connected_callback);  // Initial connect: full dump
              }
              lastNodeReportRetry = now;
          }

          // Safety net: if handshake succeeded but position config was never received
          // (e.g., ring-buffer overflow wiped it from the first dump), re-request after 20 s.
          // The 20 s window covers the first dump's completion time (~10-15 s per logs).
          static uint32_t gpsConfigRetryAt = 0;
          if (handshakeComplete && !gpsConfigAttempted && !isPositionConfigCaptured()) {
              if (gpsConfigRetryAt == 0) gpsConfigRetryAt = now + 20000;
              else if (now >= gpsConfigRetryAt) {
                  Serial.println("Config retry: position config not captured, re-requesting dump");
                  if (mesh_serial_enabled) mt_request_node_report(connected_callback);
                  gpsConfigRetryAt = now + 10000;
              }
          } else {
              gpsConfigRetryAt = 0;
          }

          // Send AWAKE once after handshake (tag 3) + GPS config
          if (can_send && handshakeComplete && gpsConfigAttempted) {
              if (!wakeNotificationSent) {
                  const char *wakeMessage = "~#01#GC#AWAKE#";
                  if (mt_send_text(wakeMessage, BROADCAST_ADDR, 0)) {
#if DEBUG_GCM_MESSAGES
                      Serial.print("GCM TX: ");
                      Serial.println(wakeMessage);
#endif
                      wakeNotificationSent = true;
                  } else {
                      Serial.println("Failed to send wake notification, will retry");
                  }
              }

              // Send REQ_WX_ENT once after GPS sync if NVM data was absent or from a previous day
              if (!reqWxEntSent && wx_eeprom_loaded && np_eeprom_loaded) {
                  if (reqWxEntNeeded) {
                      const char *reqMessage = "~#01#GC#REQ_WX_ENT#";
                      if (mt_send_text(reqMessage, BROADCAST_ADDR, 0)) {
#if DEBUG_GCM_MESSAGES
                          Serial.print("GCM TX: ");
                          Serial.println(reqMessage);
#endif
                          reqWxEntSent = true;
                      } else {
                          Serial.println("Failed to send REQ_WX_ENT, will retry");
                      }
                  } else {
                      reqWxEntSent = true;  // NVM data was current for today, no request needed
                  }
              }
          }

          // Refresh node list when a favorite was added without a known short name.
          if (can_send && nodeListRefreshRequested) {
              nodeListRefreshRequested = false;
              mt_request_node_report(nullptr);
          }

          // Drain user-initiated chat TX (canned + typed) onto the radio.
          if (can_send && chatTxQueue) {
              chatTxItem_t tx;
              while (xQueueReceive(chatTxQueue, &tx, 0) == pdTRUE) {
                  if (mt_send_text(tx.text, tx.dest, tx.channel)) {
#if DEBUG_GCM_MESSAGES
                      Serial.printf("GCM TX: ch=%u dest=%lu msg='%s'\n",
                                    (unsigned)tx.channel, (unsigned long)tx.dest, tx.text);
#endif
                  } else {
                      Serial.printf("Chat TX failed: '%s'\n", tx.text);
                  }
              }
          }

#if DEBUG_OTA_TX_TEST
          // Periodic OTA test: send T1, T2, T3... every 30s to verify OTA TX
          if (can_send && handshakeComplete && gpsConfigAttempted && now >= next_send_time) {
              static int otaTestCount = 0;
              otaTestCount++;
              uint32_t elapsed = now / 1000;
              char msg[48];
              snprintf(msg, sizeof(msg), "T%d %lus", otaTestCount, elapsed);

              bool success = mt_send_text(msg, BROADCAST_ADDR, DEBUG_OTA_TX_TEST_CHANNEL);
              Serial.printf("OTA test: %s -> %s\n", msg, success ? "sent" : "FAILED");

              next_send_time = now + 30000;
          }
#endif

          vTaskDelay(pdMS_TO_TICKS(100));
      }
  }