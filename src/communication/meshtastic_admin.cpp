#include "meshtastic_admin.h"
#include "Meshtastic.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include "globals.h"
#include "storage/favorites.h"

// External declarations from mt_protocol.cpp in meshtastic library
extern uint32_t my_node_num;

// Forward declaration - this function exists in mt_protocol.cpp
extern bool _mt_send_toRadio(meshtastic_ToRadio toRadio);

// Desired GPS configuration settings
static const GpsConfigSettings desiredGpsConfig = {
    .gps_mode = meshtastic_Config_PositionConfig_GpsMode_ENABLED,
    .fixed_position = false,
    .gps_update_interval = 8
};

// State tracking for GPS config initialization
static bool gpsConfigInitialized = false;
static uint8_t configUpdateRetries = 0;
static const uint8_t MAX_RETRIES = 50;  // 5 seconds at 100ms intervals
static bool gpsConfigSentSuccessfully = false;  // Track if config was sent (will cause reboot)

// Captured radio position config (read-modify-write pattern)
static meshtastic_Config_PositionConfig capturedPositionConfig;
static bool positionConfigCaptured = false;

// Helper function to send admin messages
static bool sendAdminMessage(meshtastic_AdminMessage *adminMsg) {
    // Encode the admin message into a temporary buffer
    pb_byte_t admin_buf[256];
    pb_ostream_t admin_stream = pb_ostream_from_buffer(admin_buf, sizeof(admin_buf));

    if (!pb_encode(&admin_stream, meshtastic_AdminMessage_fields, adminMsg)) {
        Serial.println("Failed to encode AdminMessage");
        return false;
    }

    // Create a MeshPacket with the admin message as payload
    meshtastic_MeshPacket meshPacket = meshtastic_MeshPacket_init_default;
    meshPacket.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    meshPacket.id = esp_random();
    meshPacket.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    meshPacket.to = my_node_num;  // Send to the connected radio
    meshPacket.channel = 0;
    meshPacket.decoded.payload.size = admin_stream.bytes_written;
    memcpy(meshPacket.decoded.payload.bytes, admin_buf, admin_stream.bytes_written);

    // Wrap in ToRadio and send
    meshtastic_ToRadio toRadio = meshtastic_ToRadio_init_default;
    toRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    toRadio.packet = meshPacket;

    return _mt_send_toRadio(toRadio);
}

// Passive node short-name cache. Populated by handleNodeInfo() from every
// NodeInfo broadcast that arrives over the mesh. Ring buffer — oldest entry
// overwritten when full. All accesses are from the meshtastic callback task;
// no mutex needed.
#define NODE_NAME_CACHE_SIZE 8
static struct { uint32_t nodeNum; char shortName[5]; } s_nodeNameCache[NODE_NAME_CACHE_SIZE];
static uint8_t s_nodeNameCacheCount = 0;
static uint8_t s_nodeNameCacheNext  = 0;

void handleNodeInfo(uint32_t nodeNum, const char* shortName) {
    if (!shortName || !shortName[0]) return;
    if (!favoritesContains(nodeNum)) return;
#if DEBUG_GCM_MESSAGES
    Serial.printf("[NODEINFO] nodeNum=%08x name=\"%.4s\"\n", (unsigned)nodeNum, shortName);
#endif
    for (uint8_t i = 0; i < s_nodeNameCacheCount; i++) {
        if (s_nodeNameCache[i].nodeNum == nodeNum) {
            strncpy(s_nodeNameCache[i].shortName, shortName, 4);
            s_nodeNameCache[i].shortName[4] = '\0';
            return;
        }
    }
    uint8_t idx = s_nodeNameCacheNext;
    s_nodeNameCache[idx].nodeNum = nodeNum;
    strncpy(s_nodeNameCache[idx].shortName, shortName, 4);
    s_nodeNameCache[idx].shortName[4] = '\0';
    s_nodeNameCacheNext = (s_nodeNameCacheNext + 1) % NODE_NAME_CACHE_SIZE;
    if (s_nodeNameCacheCount < NODE_NAME_CACHE_SIZE) s_nodeNameCacheCount++;
}

const char* nodeNameCacheLookup(uint32_t nodeNum) {
    for (uint8_t i = 0; i < s_nodeNameCacheCount; i++) {
        if (s_nodeNameCache[i].nodeNum == nodeNum) return s_nodeNameCache[i].shortName;
    }
    return "";
}

// Called from handle_channel_tag() in mt_protocol.cpp for each channel received
// from the GCM (both during the boot config dump and in response to get_channel_request)
void handleChannelResponse(meshtastic_Channel *channel) {
    // Channel data available via getChannelName() for BLE settings push
    (void)channel;
}

bool mt_send_admin_reboot(int32_t seconds) {
    meshtastic_AdminMessage adminMsg = meshtastic_AdminMessage_init_default;
    adminMsg.which_payload_variant = meshtastic_AdminMessage_reboot_seconds_tag;
    adminMsg.reboot_seconds = seconds;

#if DEBUG_GCM_MESSAGES
    Serial.printf("GCM TX: Admin reboot command (%d seconds delay)\n", (int)seconds);
#endif

    return sendAdminMessage(&adminMsg);
}

bool mt_set_position_config(const meshtastic_Config_PositionConfig *config) {
    meshtastic_AdminMessage adminMsg = meshtastic_AdminMessage_init_default;
    adminMsg.which_payload_variant = meshtastic_AdminMessage_set_config_tag;
    adminMsg.set_config.which_payload_variant = meshtastic_Config_position_tag;
    adminMsg.set_config.payload_variant.position = *config;

    return sendAdminMessage(&adminMsg);
}

// Admin portnum callback to handle ADMIN_APP messages
// Currently a placeholder - kept for potential future admin message handling
void admin_portnum_callback(uint32_t from, uint32_t to, uint8_t channel,
                           meshtastic_PortNum port, meshtastic_Data_payload_t *payload) {
    // Only process actual ADMIN_APP messages (port 6)
    if (port != meshtastic_PortNum_ADMIN_APP) {
        return;
    }

    // Guard against NULL payload (can happen during GCM reconnection with corrupted packets)
    if (payload == nullptr) {
        Serial.println("*** admin_portnum_callback: NULL payload received ***");
        return;
    }

    // If we ever receive an ADMIN_APP message, log it for debugging
    meshtastic_AdminMessage adminMsg = meshtastic_AdminMessage_init_default;
    pb_istream_t stream = pb_istream_from_buffer(payload->bytes, payload->size);

    if (pb_decode(&stream, meshtastic_AdminMessage_fields, &adminMsg)) {
#if DEBUG_GCM_MESSAGES
        Serial.printf("GCM RX: ADMIN_APP message, variant=%d\n", adminMsg.which_payload_variant);
#endif
    }
}

// Callback from mt_protocol.cpp when config_complete_id (tag 7) is received
// Tag 7 is unreliable on serial connections but handled if it arrives
void handleConfigComplete() {
    // Tag 3 already sets handshakeComplete, but log if tag 7 does arrive
    if (handshakeComplete) {
        Serial.println("GCM config_complete (tag 7) received");
    }
}

// Callback from mt_protocol.cpp when FromRadio.config (position) is received
// Called during connection handshake (tag 5) — captures radio's full PositionConfig
// so we can do read-modify-write instead of zeroing all fields
void handlePositionConfigResponse(meshtastic_Config_PositionConfig *config) {
    if (config == nullptr) return;
    capturedPositionConfig = *config;
    positionConfigCaptured = true;
    Serial.printf("GCM position config captured (interval=%d, gps_mode=%d)\n",
                  config->gps_update_interval, config->gps_mode);
}

// Get the radio's captured position config (for read-modify-write pattern)
// Returns true and copies config to *out if captured, false otherwise
bool getRadioPositionConfig(meshtastic_Config_PositionConfig *out) {
    if (!positionConfigCaptured || out == nullptr) return false;
    *out = capturedPositionConfig;
    return true;
}

bool isPositionConfigCaptured() { return positionConfigCaptured; }

// Callback from mt_protocol.cpp when FromRadio.metadata is received
// Called by mt_protocol.cpp line 488 when metadata is received from radio
// Note: Currently unused - GCM firmware doesn't send metadata
// Must exist to satisfy linker since mt_protocol.cpp unconditionally calls it
void handleDeviceMetadata(meshtastic_DeviceMetadata *metadata) {
    // Empty placeholder - GCM never sends metadata
}

// Callback from mt_protocol.cpp when FromRadio.my_info (tag 3) is received
// Tag 3 is the first tag in every config dump — its arrival proves the GCM serial
// interface is alive and ready. We use it as our "handshake complete" signal since
// tag 7 (config_complete_id) is unreliable on serial connections.
void handleMyNodeInfo(meshtastic_MyNodeInfo *myNodeInfo) {
    if (myNodeInfo == nullptr) {
        return;
    }

    // Tag 3 = GCM serial interface is ready for commands
    if (!handshakeComplete) {
        handshakeComplete = true;
        not_yet_connected = false;
        Serial.println("GCM handshake complete (tag 3) - sends enabled");
    }

    // Convert node number to hex string with ! prefix (e.g., !a1b2c3d4)
    uint32_t nodeNum = myNodeInfo->my_node_num;
    char nodeIdStr[12];
    snprintf(nodeIdStr, sizeof(nodeIdStr), "!%08x", nodeNum);

    set_var_gcm_node_id(nodeIdStr);
#if DEBUG_MESHTASTIC_CONNECTION
    Serial.print("GCM Node ID: ");
    Serial.println(nodeIdStr);
#endif
}

// Callback from mt_protocol.cpp when GCM reboots
// Reset state to allow re-capturing node ID and resending wake notification after reconnection
//
// GCM boot sequence after GPS config change:
//   GPS-config-induced reboot (only if interval needs changing)
// gpsConfigAttempted is set either by GPS config skip (interval matches)
// or by handleGcmRebooted after GPS-config-induced reboot
void handleGcmRebooted() {
#if DEBUG_MESHTASTIC_CONNECTION
    Serial.println("*** GCM REBOOTED - Resetting state for reconnection ***");
#endif

    // Block all sends until new tag 3 arrives
    handshakeComplete = false;
    not_yet_connected = true;  // re-enable retry loop in case tag 3 is slow to arrive

    // Clear the stored node ID to indicate stale data
    set_var_gcm_node_id("");

    // Clear captured config — will be re-captured during reconnect handshake
    positionConfigCaptured = false;

    // If GPS config was sent successfully, this reboot is the GPS-config-induced reboot
    // System is now stable, allow AWAKE to be sent
    if (gpsConfigSentSuccessfully) {
        gpsConfigAttempted = true;
        gpsConfigSentSuccessfully = false;  // Reset flag
    }

    // Reset wake/request flags so they will be sent again on reconnection
    wakeNotificationSent = false;
    reqWxEntSent = false;

}

void initGpsConfigOnBoot() {
    if (gpsConfigInitialized) return;
    if (not_yet_connected) return;
    if (!handshakeComplete) return;        // Wait for tag 3 (GCM serial ready)
    if (!positionConfigCaptured) return;   // Wait for tag 5 (position config data)

    // Check if radio already has our desired interval (e.g., from a previous boot)
    // If so, skip the config write entirely — no reboot needed
    if (capturedPositionConfig.gps_update_interval == desiredGpsConfig.gps_update_interval) {
        Serial.println("GPS Config Init: Radio already has correct interval - skipping");
        gpsConfigInitialized = true;
        gpsConfigAttempted = true;
        return;
    }

    // Retry limit
    if (configUpdateRetries >= MAX_RETRIES) {
        Serial.println("GPS Config Init: Failed after max retries - giving up");
        gpsConfigInitialized = true;
        gpsConfigAttempted = true;     // No reboot will occur, allow AWAKE to be sent now
        return;
    }

    // Use captured config as base, only modify gps_update_interval
    // This preserves all other fields (tx_gpio, rx_gpio, gps_en_gpio, position_flags, etc.)
    meshtastic_Config_PositionConfig config = capturedPositionConfig;
    config.gps_update_interval = desiredGpsConfig.gps_update_interval;

    if (mt_set_position_config(&config)) {
        Serial.println("GPS Config Init: Complete");
#if DEBUG_GCM_MESSAGES
        Serial.printf("GCM TX: GPS config (interval %d -> %d, preserving all other fields)\n",
                      capturedPositionConfig.gps_update_interval, config.gps_update_interval);
#endif
        gpsConfigInitialized = true;
        gpsConfigSentSuccessfully = true;  // Flag that GCM will reboot due to config change
        // DO NOT set gpsConfigAttempted here - wait for GPS-config-induced reboot (2nd reboot)
    } else {
        configUpdateRetries++;
        if (configUpdateRetries == 1) {
            Serial.println("GPS Config Init: Sending config to Meshtastic radio...");
        }
    }
}

// Capture node ID once after GCM connection is established
// Called by system task polling
// Note: Only node ID is available - GCM firmware doesn't send DeviceMetadata messages
void requestMetadataOnce() {
    static bool nodeIdCaptured = false;

    // Only run once, and only after connection is established
    if (!nodeIdCaptured && !not_yet_connected) {
        // Capture node ID from my_node_num (set during connection handshake)
        if (my_node_num != 0) {
            char nodeIdStr[12];
            snprintf(nodeIdStr, sizeof(nodeIdStr), "!%08x", my_node_num);
            set_var_gcm_node_id(nodeIdStr);
#if DEBUG_MESHTASTIC_CONNECTION
            Serial.print("GCM Node ID: ");
            Serial.println(nodeIdStr);
#endif
            nodeIdCaptured = true;
        }
    }
}

// Send a lightweight want_config_id request that asks GCM to resend only my_info (tag 3),
// skipping the full node database. Uses SPECIAL_NONCE (69420) defined in mt_protocol.cpp.
// Use this after a mid-session GCM reboot instead of mt_request_node_report.
bool mt_request_my_node_info() {
    meshtastic_ToRadio toRadio = meshtastic_ToRadio_init_default;
    toRadio.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    toRadio.want_config_id = 69420;  // SPECIAL_NONCE from mt_protocol.cpp
    return _mt_send_toRadio(toRadio);
}

bool resetGpsIntervalBeforeSleep() {
    meshtastic_Config_PositionConfig config;
    if (!getRadioPositionConfig(&config)) {
        Serial.println("GPS interval reset skipped - no captured config");
        return false;
    }
    config.gps_update_interval = 120;  // 120 seconds = Meshtastic default (protobuf3 won't encode 0)

#if DEBUG_GCM_MESSAGES
    Serial.println("GCM TX: GPS config (set interval to 2 min default) before sleep");
#endif

    if (mt_set_position_config(&config)) {
#if DEBUG_GCM_MESSAGES
        Serial.println("GCM TX: GPS interval reset successful");
#endif
        return true;
    } else {
        Serial.println("GPS interval reset failed");
        return false;
    }
}
