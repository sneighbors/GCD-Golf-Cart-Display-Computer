#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

void initSpeaker();
void beep(int numBeeps, uint32_t frequency, uint32_t duration, uint32_t pauseMs);
void setBacklight(uint32_t value);

// Predefined tone functions
void tone_startup();
void tone_message();
void tone_alert();
void tone_urgent();
void tone_confirm();
void tone_click();
void tone_error();

#endif // DISPLAY_H
