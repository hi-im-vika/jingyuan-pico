// main.cpp
// controller for an LED strip in a prop for a certain gacha game character's cosplay

#include <Arduino.h>
#include <FastLED.h>
#include <EEPROM.h>

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#define LED_COUNT 142   // 200 strip
//#define LED_COUNT 171 // 240 strip
//#define LED_COUNT 32
#define ONBOARD_LED_COUNT 1

#define LED_PIN 5                   // data pin for LED strip
#define SENSE_PIN 6                 // sense pin to detect if strip is connected
#define PATT_PIN 4                  // button pin to switch animations
#define MIC_PIN 26                  // mic pin for sound reactive fx
#define ONBOARD_NEOPIXEL_PIN    16  // pin for onboard WS2812-2020 on RP2040-Zero

#define FRAMES_PER_SECOND 120
#define PRIMARY_HUE 29

#define DEBOUNCE_DELAY 10

#define SOUND_DC_OFFSET  0              // DC offset in mic signal - if unusure, leave 0
#define SOUND_NOISE     30              // Noise/hum/interference in mic signal and increased value until it went quiet
#define SOUND_SAMPLES   60              // Length of buffer for dynamic level adjustment
#define SOUND_TOP (LED_COUNT + 2)       // Allow dot to go slightly off scale
#define SOUND_PEAK_FALL 5               // Rate of sound_peak falling dot

enum anim_state {
    DISCONNECTED,
    UP,
    DOWN,
    RAINBOW_IN,
    RAINBOW_OUT,
    STOP
};

// globals
// state machine
anim_state state = DISCONNECTED;
anim_state startup = UP;
unsigned long pressed_millis = 0;
unsigned long frame_millis = 0;
unsigned long pulse_millis = 0;

// anims
uint8_t sound_peak = 0;                                              // Used for falling dot
uint8_t sound_dot_count = 0;                                              // Frame counter for delaying dot-falling speed
uint8_t sound_vol_count = 0;                                              // Frame counter for storing past volume data
int sound_vol[SOUND_SAMPLES];                                              // Collection of prior volume samples
int sound_lvl = 10;                                             // Current "dampened" audio level
int sound_min_lvl_avg = 0;                                              // For dynamic adjustment of graph low & high
int sound_max_lvl_avg = 2048;

uint8_t startup_next_led = 0;
bool do_startup = true;

// debounce
bool pressed = false;
bool acted = false;

uint16_t frame_delay = 10.0f;
uint16_t frame_delay_rt = 10.0f;
uint16_t frame_delay_2 = 10.0f;
uint8_t startup_brightness = 0;
const uint8_t fps_limit = 1000 / FRAMES_PER_SECOND;
CRGB strip[LED_COUNT];
CRGB onboard[ONBOARD_LED_COUNT];

// forward function declarations, from fastled demo
void patt_solid();
void patt_pulse();
void patt_chase();
void patt_breathing();
void patt_sound();
void patt_rainbow();
void next_pattern();
void poll_button();

// pattern list from fastled demo
typedef void (*pattern_list_t[])();
pattern_list_t patterns = {
        patt_solid,
        patt_pulse,
        patt_chase,
        patt_breathing,
        patt_sound,
        patt_rainbow
};
uint8_t current_pattern_idx = 0;

void setup() {
    analogReadResolution(12);
    pinMode(SENSE_PIN, INPUT_PULLUP);
    pinMode(PATT_PIN, INPUT_PULLUP);
//    pinMode(LED_BUILTIN, OUTPUT);     // only needed if using original pi pico board
    CFastLED::addLeds<NEOPIXEL, LED_PIN>(strip, LED_COUNT);
    CFastLED::addLeds<NEOPIXEL, ONBOARD_NEOPIXEL_PIN>(onboard, ONBOARD_LED_COUNT);
    FastLED.clear();
    FastLED.show();     // turn off all LEDs ASAP
    EEPROM.begin(1);    // read last chosen animation
    current_pattern_idx = EEPROM.read(0);
    if ((current_pattern_idx < 0) || (current_pattern_idx >= ARRAY_SIZE(patterns))) current_pattern_idx = 0;
}

void loop() {
    // while strip is connected
    while (digitalRead(SENSE_PIN) == LOW) {
        // turn on debug led when strip connected
        onboard[0] = CRGB(0,1,0);
        poll_button();

        EVERY_N_MILLIS(fps_limit) {
            patterns[current_pattern_idx]();
            FastLED.show();
        }
        yield();
    }

    // as soon as strip disconnects
    onboard[0] = CRGB(1,0,0);
    FastLED.show();
    startup_brightness = 0;
    startup_next_led = 0;
    do_startup = true;
    startup = UP;
    yield();
}

void next_pattern() {
    current_pattern_idx = (current_pattern_idx + 1) % ARRAY_SIZE(patterns);
}

void poll_button() {
    // debounce tomfoolery
    if (digitalRead(PATT_PIN) == LOW && !pressed) {
        pressed = true;
        pressed_millis = millis();
    }

    // switch anim
    if (pressed) {
        if (millis() - pressed_millis > DEBOUNCE_DELAY) {
            if (digitalRead(PATT_PIN) == LOW && !acted) {
                next_pattern();
                acted = true;
                EEPROM.write(0, current_pattern_idx);
                EEPROM.commit();
            } else if (digitalRead(PATT_PIN) == HIGH && acted) {
                pressed = false;
                acted = false;
            }
        }
    }
}

void patt_solid() {
    fill_solid(strip,LED_COUNT,CHSV(PRIMARY_HUE,255,255));
}

void patt_pulse() {
    fill_solid(strip,LED_COUNT,CHSV(20,255,255));
}

void patt_chase() {
    fill_solid(strip,LED_COUNT,CHSV(30,255,255));
}

void patt_breathing() {
    fill_solid(strip,LED_COUNT,CHSV(40,255,255));
}

// based on neopixel sound reactive pendant and https://github.com/atuline/FastLED-SoundReactive
void patt_sound() {

    uint8_t i;
    uint16_t minLvl, maxLvl;
    int n, height;

    n = analogRead(MIC_PIN);                                    // Raw reading from mic
    n = abs(n - 2048 - SOUND_DC_OFFSET);                               // Center on zero

    n = (n <= SOUND_NOISE) ? 0 : (n - SOUND_NOISE);                         // Remove noise/hum
    sound_lvl = ((sound_lvl * 7) + n) >> 3;                                 // "Dampened" reading (else looks twitchy)

    // Calculate bar height based on dynamic min/max levels (fixed point):
    height = SOUND_TOP * (sound_lvl - sound_min_lvl_avg) / (long) (sound_max_lvl_avg - sound_min_lvl_avg);

    if (height < 0L) height = 0;                          // Clip output
    else if (height > SOUND_TOP) height = SOUND_TOP;
    if (height > sound_peak) sound_peak = height;                     // Keep 'sound_peak' dot at top


    // Color pixels based on rainbow gradient
    for (i = 0; i < LED_COUNT; i++) {
        if (i >= height) strip[i].setRGB(0, 0, 0);
        else strip[i] = CHSV(PRIMARY_HUE, 255, 255);
    }

    // Draw sound_peak dot
    if (sound_peak > 0 && sound_peak <= LED_COUNT - 1) strip[sound_peak] = CHSV(0, 0, 255);

// Every few frames, make the sound_peak pixel drop by 1:

    if (++sound_dot_count >= SOUND_PEAK_FALL) {                            // fall rate
        if (sound_peak > 0) sound_peak--;
        sound_dot_count = 0;
    }

    sound_vol[sound_vol_count] = n;                                          // Save sample for dynamic leveling
    if (++sound_vol_count >= SOUND_SAMPLES) sound_vol_count = 0;                    // Advance/rollover sample counter

    // Get volume range of prior frames
    minLvl = maxLvl = sound_vol[0];
    for (i = 1; i < SOUND_SAMPLES; i++) {
        if (sound_vol[i] < minLvl) minLvl = sound_vol[i];
        else if (sound_vol[i] > maxLvl) maxLvl = sound_vol[i];
    }
    // minLvl and maxLvl indicate the volume range over prior frames, used
    // for vertically scaling the output graph (so it looks interesting
    // regardless of volume level).  If they're too close together though
    // (e.g. at very low volume levels) the graph becomes super coarse
    // and 'jumpy'...so keep some minimum distance between them (this
    // also lets the graph go to zero when no sound is playing):
    if ((maxLvl - minLvl) < SOUND_TOP) maxLvl = minLvl + SOUND_TOP;
    sound_min_lvl_avg = (sound_min_lvl_avg * 63 + minLvl) >> 6;                 // Dampen min/max levels
    sound_max_lvl_avg = (sound_max_lvl_avg * 63 + maxLvl) >> 6;                 // (fake rolling average)

}

void patt_rainbow() {
    fill_solid(strip,LED_COUNT,CHSV(50,255,255));
}