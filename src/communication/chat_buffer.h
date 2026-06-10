#ifndef CHAT_BUFFER_H
#define CHAT_BUFFER_H

#include <Arduino.h>
#include "types.h"

// Filter indices — used by BLE CMD_REQ_CHAT and chatBufferSnapshot():
//   0 = DM (direct messages)
//   1 = ALL
//   2 = CHANNEL 0
//   3 = CHANNEL 1
//   4 = CHANNEL 2
#define CHAT_FILTER_DM    0
#define CHAT_FILTER_ALL   1
#define CHAT_FILTER_CH0   2
#define CHAT_FILTER_CH1   3
#define CHAT_FILTER_CH2   4

// Maximum unread DMs tracked by the side-array (also the NVS persistence cap).
#define DM_SLOT_MAX 4

// Append a message to the ring buffer. Assigns msg.id and msg.timestamp
// (the latter only if the caller left it at 0). The text field of the
// stored copy is run through chatAbbreviate() so HoT/GC packets show up
// as short tags in the chat list.
void chatBufferAppend(const chatMessage_t *msg);

// Copy up to maxN messages matching `filter` into out[], in chronological
// order (oldest first, newest last). Returns the number of entries written.
size_t chatBufferSnapshot(uint8_t filter, chatMessage_t *out, size_t maxN);

// Look up by id. Returns true if found and not tombstoned.
bool chatBufferGetById(uint32_t id, chatMessage_t *out);

// Mark a message as read by id. No-op if id not found or already read.
void chatBufferMarkRead(uint32_t id);

// Returns the number of unread incoming DMs (to == my_node_num, !outgoing, !read).
size_t chatBufferUnreadDmCount(void);

// Copy up to maxN unread incoming DMs into out[], oldest first.
// Used by saveDmsToNvs() to persist across deep sleep.
size_t chatBufferSnapshotUnreadDms(chatMessage_t *out, size_t maxN);

// Transform HoT/GC packet text into a short display tag. Plain user
// text passes through truncated. dst is always null-terminated.
void chatAbbreviate(const char *src, char *dst, size_t dstSize);

#endif // CHAT_BUFFER_H
