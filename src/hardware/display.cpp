#include "display.h"
#include "config.h"
#include "get_set_vars.h"

// Beep control variables
static int beepCount = 0;
static int targetBeeps = 0;
static bool beepOn = false;
static hw_timer_t *beepTimer = NULL;
static uint32_t currentBeepFrequency = BEEP_FREQUENCY_HZ;
static uint32_t currentBeepDuration = BEEP_DURATION_MS;
static uint32_t currentBeepPause = 200;
static int32_t currentBeepVolume = 10;

// Hardware timer ISR - must be fast, no Serial.printf allowed
void IRAM_ATTR beepTimerISR() {
    if (beepOn) {
        ledcWrite(SPEAKER_LEDC_CHANNEL, 0);
        beepOn = false;
        beepCount++;

        if (beepCount < targetBeeps) {
            timerWrite(beepTimer, 0);
            timerAlarmWrite(beepTimer, currentBeepPause * 1000, false);
            timerAlarmEnable(beepTimer);
        } else {
            timerAlarmDisable(beepTimer);
            beepCount = 0;
            targetBeeps = 0;
        }
    } else {
        if (beepCount < targetBeeps) {
            if (currentBeepVolume <= 0) {
                ledcWrite(SPEAKER_LEDC_CHANNEL, 0);
            } else {
                uint32_t maxDuty = (1 << SPEAKER_LEDC_TIMER_BIT) / 2;
                uint32_t volumeDuty = (maxDuty * currentBeepVolume) / 100;
                ledcWrite(SPEAKER_LEDC_CHANNEL, volumeDuty);
            }
            beepOn = true;

            timerWrite(beepTimer, 0);
            timerAlarmWrite(beepTimer, currentBeepDuration * 1000, false);
            timerAlarmEnable(beepTimer);
        }
    }
}

void initSpeaker() {
#if ESP_IDF_VERSION_MAJOR == 5
    ledcAttach(SPEAKER_PIN, BEEP_FREQUENCY_HZ, SPEAKER_LEDC_TIMER_BIT);
#else
    ledcSetup(SPEAKER_LEDC_CHANNEL, BEEP_FREQUENCY_HZ, SPEAKER_LEDC_TIMER_BIT);
    ledcAttachPin(SPEAKER_PIN, SPEAKER_LEDC_CHANNEL);
#endif

    beepTimer = timerBegin(0, 80, true);
    timerAttachInterrupt(beepTimer, &beepTimerISR, true);
    timerStart(beepTimer);

#if DEBUG_INIT == 1
    Serial.println("Speaker initialized");
#endif
}

// No-op: backlight hardware removed with CYD display
void setBacklight(uint32_t) {}

static void beep_internal(int numBeeps, uint32_t frequency, uint32_t duration, uint32_t pauseMs, int volume) {
    if (numBeeps <= 0 || beepTimer == NULL) return;

    if (frequency != currentBeepFrequency) {
        currentBeepFrequency = frequency;
#if ESP_IDF_VERSION_MAJOR == 5
        ledcChangeFrequency(SPEAKER_PIN, frequency, SPEAKER_LEDC_TIMER_BIT);
#else
        ledcSetup(SPEAKER_LEDC_CHANNEL, frequency, SPEAKER_LEDC_TIMER_BIT);
#endif
    }

    currentBeepDuration = duration;
    currentBeepPause = pauseMs;
    currentBeepVolume = volume;

    timerAlarmDisable(beepTimer);
    ledcWrite(SPEAKER_LEDC_CHANNEL, 0);

    beepCount = 0;
    targetBeeps = numBeeps;
    beepOn = false;

    timerWrite(beepTimer, 0);
    timerAlarmWrite(beepTimer, 1000, false);
    timerAlarmEnable(beepTimer);
}

void beep(int numBeeps, uint32_t frequency, uint32_t duration, uint32_t pauseMs) {
    beep_internal(numBeeps, frequency, duration, pauseMs, speaker_volume);
}

static void playTone(int numBeeps, uint32_t frequency, uint32_t duration, uint32_t pauseMs, int volumePercent) {
    int effectiveVolume = (speaker_volume * volumePercent) / 100;
    beep_internal(numBeeps, frequency, duration, pauseMs, effectiveVolume);
}

void tone_startup() { playTone(1, 2500, 40,  0,   70); }
void tone_message() { playTone(2, 2200, 50,  150, 80); }
void tone_alert()   { playTone(1, 1500, 250, 100, 50); }
void tone_urgent()  { playTone(4, 1500, 200, 100, 80); }
void tone_confirm() { playTone(1, 2200, 50,  0,   60); }
void tone_click()   { playTone(1, 20,   51,  0,   40); }
void tone_error()   { playTone(1, 90,   150, 0,   10); }
