// main.cpp
// controller for an LED strip in a prop for a certain gacha game character's
// cosplay

#include <Arduino.h>
#include <EEPROM.h>
#include <FastLED.h>

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

// #define LED_COUNT 142   // 200 strip
#define LED_COUNT 171 // 240 strip
// #define LED_COUNT 32
#define ONBOARD_LED_COUNT 1

#define LED_PIN 24   // data pin for LED strip
#define SENSE_PIN 25 // sense pin to detect if strip is connected
#define PATT_PIN 17  // button pin to switch animations
#define MIC_PIN 26   // mic pin for sound reactive fx
// #define ONBOARD_NEOPIXEL_PIN    16  // pin for onboard WS2812-2020 on
// RP2040-Zero
#define STRIP_CONN_PIN 5
#define BRIGHTUP_PIN 16
#define BRIGHTDN_PIN 20
#define PATTNEXT_PIN 18
#define PATTPREV_PIN 19

#define PRIMARY_HUE 29
#define PRIMARY_SPEC_HUE 26
#define SWEEP_FADE_BY 255

#define BEATSIN_PHASE 128
#define BEATSIN_INCREMENT 16

#define WAVE_MAX 255
#define WAVE_MIN 32

#define PATT_IDX_RAINBOW 8
#define PATT_IDX_RAINBOW_DUAL (PATT_IDX_RAINBOW + 1)

#define SCROLL_UPDATE_TIME 9
#define KNIGHTRIDER_UPDATE_TIME 5
#define KNIGHTRIDER_WIDTH 20
#define RAINBOW_UPDATE_TIME 10
#define RAINBOW_DUAL_UPDATE_TIME 5
#define FADE_UPDATE_TIME 5

#define DEBOUNCE_DELAY 10

#define SOUND_DC_OFFSET 0 // DC offset in mic signal - if unusure, leave 0
#define SOUND_NOISE                                                            \
  30 // Noise/hum/interference in mic signal and increased value until it went
     // quiet
#define SOUND_SAMPLES 60    // Length of buffer for dynamic level adjustment
#define SOUND_TOP LED_COUNT // Allow dot to go slightly off scale
#define SOUND_PEAK_FALL 4   // Rate of sound_peak falling dot
#define SOUND_PEAK_TIMEOUT 1000

enum sweep_state_t { UP, DOWN, STOP };

// globals
// state machine
sweep_state_t sweep_state = UP;
unsigned long pressed_millis = 0;
const CHSV PRIMARY_HSV = CHSV(PRIMARY_HUE, 255, 255);
const CRGB PRIMARY_RGB = hsv2rgb_rainbow(PRIMARY_HSV);

// anims
uint8_t rainbow_hue = 0;
uint8_t knightrider_idx = 0;
bool knightrider_rev = false;
uint8_t fade_progress = 255;
bool fade_rev = false;
uint8_t wave_offset = 255;
uint8_t confetti_hue = 0;

uint8_t sound_peak = 0;       // Used for falling dot
uint8_t sound_dot_count = 0;  // Frame counter for delaying dot-falling speed
uint8_t sound_vol_count = 0;  // Frame counter for storing past volume data
int sound_vol[SOUND_SAMPLES]; // Collection of prior volume samples
int sound_lvl = 10;           // Current "dampened" audio level
int sound_min_lvl_avg = 0;    // For dynamic adjustment of graph low & high
int sound_max_lvl_avg = 2048;
unsigned long peak_millis = 0;

uint8_t sweep_idx = 0;
uint8_t startup_idx = 0;
bool do_startup = true;

int global_brightness = 255;

// debounce
bool pressed = false;
bool acted_patt = false;
bool acted_brightup = false;
bool acted_brightdn = false;
bool acted_pattnext = false;
bool acted_pattprev = false;

uint8_t startup_rainbow_brightness = 0;
CRGBArray<LED_COUNT> strip;
CRGB onboard[ONBOARD_LED_COUNT];

// forward function declarations, from fastled demo
void patt_solid();
void patt_solid_glitter();
void patt_scroll();
void patt_knightrider();
void patt_beatsin8_one();
void patt_beatsin8_four();
void patt_fade();
void patt_sound();
void patt_rainbow();
void patt_rainbow_dual();
void patt_confetti();

void patt_startup();
void next_pattern();
void poll_button();

// pattern list from fastled demo
typedef void (*pattern_list_t[])();
pattern_list_t patterns = {
    patt_solid,        patt_solid_glitter, patt_scroll,  patt_knightrider,
    patt_beatsin8_one, patt_beatsin8_four, patt_fade,    patt_sound,
    patt_rainbow,      patt_rainbow_dual,  patt_confetti};
uint8_t current_pattern_idx = 0;
//
// void setup1() {
//    Serial.begin(115200);
//}
//
// void loop1() {
//    Serial.println(fade_progress);
//}

void setup() {
  analogReadResolution(12);
  pinMode(SENSE_PIN, INPUT_PULLUP);
  pinMode(PATT_PIN, INPUT_PULLUP);
  pinMode(BRIGHTUP_PIN, INPUT_PULLUP);
  pinMode(BRIGHTDN_PIN, INPUT_PULLUP);
  pinMode(STRIP_CONN_PIN, OUTPUT);
  //    pinMode(LED_BUILTIN, OUTPUT);     // only needed if using original pi
  //    pico board
  CFastLED::addLeds<NEOPIXEL, LED_PIN>(strip, LED_COUNT);
  // CFastLED::addLeds<NEOPIXEL, ONBOARD_NEOPIXEL_PIN>(onboard,
  // ONBOARD_LED_COUNT);
  FastLED.setBrightness(255);
  FastLED.clear();
  FastLED.show();  // turn off all LEDs ASAP
  EEPROM.begin(1); // read last chosen animation
  current_pattern_idx = EEPROM.read(0);
  if ((current_pattern_idx < 0) ||
      (current_pattern_idx >= ARRAY_SIZE(patterns)))
    current_pattern_idx = 0;
}

void loop() {
  // while strip is connected
  while (digitalRead(SENSE_PIN) == LOW) {
    // turn on debug led when strip connected
    // onboard[0] = CRGB(0,1,0);
    digitalWrite(STRIP_CONN_PIN, HIGH);
    poll_button();

    if (do_startup) {
      patt_startup();
    } else {
      if (startup_idx < LED_COUNT)
        startup_idx++;
      patterns[current_pattern_idx]();
    }
    FastLED.setBrightness(global_brightness);
    FastLED.show();
    yield();
  }

  // as soon as strip disconnects, do cleanup
  // onboard[0] = CRGB(1,0,0);
  digitalWrite(STRIP_CONN_PIN, LOW);
  FastLED.show();
  if ((current_pattern_idx == PATT_IDX_RAINBOW) ||
      (current_pattern_idx == (PATT_IDX_RAINBOW_DUAL))) {
    startup_rainbow_brightness = 0;
  }
  sweep_idx = 0;
  startup_idx = 0;
  do_startup = true;
  sweep_state = UP;

  // let microcontroller do its own thing in the meantime
  while (digitalRead(SENSE_PIN) == HIGH) {
    yield();
  }
}

void next_pattern() {
  current_pattern_idx = (current_pattern_idx + 1) % ARRAY_SIZE(patterns);
}

void poll_button() {
  // debounce tomfoolery
  bool any_button_pressed =
      digitalRead(BRIGHTUP_PIN) == LOW || digitalRead(BRIGHTDN_PIN) == LOW ||
      digitalRead(PATTNEXT_PIN) == LOW || digitalRead(PATTPREV_PIN) == LOW ||
      digitalRead(PATT_PIN) == LOW;
  if (any_button_pressed && !pressed) {
    pressed = true;
    pressed_millis = millis();
  }

  // switch anim
  if (pressed) {
    if (millis() - pressed_millis > DEBOUNCE_DELAY) {
      if (digitalRead(PATT_PIN) == LOW && !acted_patt) {
        next_pattern();
        acted_patt = true;
        EEPROM.write(0, current_pattern_idx);
        EEPROM.commit();
      } else if (digitalRead(PATT_PIN) == HIGH && acted_patt) {
        pressed = false;
        acted_patt = false;
      } else if (digitalRead(BRIGHTDN_PIN) == LOW && !acted_brightdn) {
        if (global_brightness - 32 >= 0) {
          global_brightness -= 32;
        } else {
          global_brightness = 0;
        }
        acted_brightdn = true;
      } else if (digitalRead(BRIGHTDN_PIN) == HIGH && acted_brightdn) {
        pressed = false;
        acted_brightdn = false;
      }
    }
  }
}

void patt_startup() {
  if (sweep_state != STOP) {
    switch (sweep_state) {
    case UP:
      if (sweep_idx + 1 < LED_COUNT) {
        fadeToBlackBy(strip, LED_COUNT, SWEEP_FADE_BY);
        strip[sweep_idx++] = CRGB::White;
      } else {
        sweep_idx--;
        sweep_state = DOWN;
      }
      break;
    case DOWN:
      fadeToBlackBy(strip, LED_COUNT, SWEEP_FADE_BY);
      if (sweep_idx - 1 > 0) {
        strip[sweep_idx--] = CRGB::White;
      } else if (sweep_idx == 0) {
        sweep_state = STOP;
      } else {
        sweep_idx = 0;
        strip[sweep_idx] = CRGB::White;
      }
      break;
    default:
      break;
    }
  } else {
    switch (current_pattern_idx) {
    case PATT_IDX_RAINBOW:
      if (startup_rainbow_brightness < 255) {
        fl::fill_rainbow_circular(strip, LED_COUNT, rainbow_hue, false);
        FastLED.setBrightness(++startup_rainbow_brightness);
      } else {
        startup_idx = LED_COUNT;
        do_startup = false;
      }
      break;
    case (PATT_IDX_RAINBOW_DUAL):
      if (startup_rainbow_brightness < 255) {
        fl::fill_rainbow_circular(strip(0, LED_COUNT / 2), LED_COUNT / 2,
                                  rainbow_hue, true);
        fl::fill_rainbow_circular(strip(LED_COUNT / 2 + 1, LED_COUNT),
                                  LED_COUNT / 2, rainbow_hue, false);
        FastLED.setBrightness(++startup_rainbow_brightness);
      } else {
        startup_idx = LED_COUNT;
        do_startup = false;
      }
      break;
    default:
      fadeToBlackBy(strip, LED_COUNT, 255);
      do_startup = false;
      break;
    }
  }
}

void patt_solid() {
  fill_solid(strip, startup_idx, CHSV(PRIMARY_HUE, 255, 255));
}

void patt_solid_glitter() {
  fill_solid(strip, startup_idx, CHSV(PRIMARY_HUE, 255, 255));
  if (random8() < 80) {
    uint8_t rand_led = random16(LED_COUNT);
    if (rand_led < startup_idx)
      strip[rand_led] += CRGB::White;
  }
}

void patt_scroll() {
  for (int i = 0; i < LED_COUNT; i++) {
    if (i < startup_idx) {
      strip[i] = hsv2rgb_spectrum(CHSV(
          PRIMARY_SPEC_HUE, 255,
          map(quadwave8(5 * i + wave_offset), 0, 255, WAVE_MIN, WAVE_MAX)));
    }
  }
  EVERY_N_MILLIS(SCROLL_UPDATE_TIME) wave_offset--;
}

void patt_knightrider() {
  fadeToBlackBy(strip, LED_COUNT, 255);
  if (startup_idx < KNIGHTRIDER_WIDTH) {
    fl::fill_solid(strip(0, startup_idx), startup_idx, PRIMARY_HSV);
  } else {
    fl::fill_solid(
        strip(knightrider_idx, knightrider_idx + KNIGHTRIDER_WIDTH + 1),
        KNIGHTRIDER_WIDTH, PRIMARY_HSV);
  }
  EVERY_N_MILLIS(KNIGHTRIDER_UPDATE_TIME) {
    if (knightrider_rev) {
      if (knightrider_idx - 1 > 0) {
        knightrider_idx--;
      } else if (knightrider_idx == 0) {
        knightrider_rev = false;
      } else {
        knightrider_idx = 0;
      }
    } else {
      if (knightrider_idx + 1 + KNIGHTRIDER_WIDTH < LED_COUNT) {
        knightrider_idx++;
      } else if (knightrider_idx == LED_COUNT - KNIGHTRIDER_WIDTH) {
        knightrider_rev = true;
      } else {
        knightrider_idx = LED_COUNT - KNIGHTRIDER_WIDTH;
      }
    }
  }
}

void patt_beatsin8_one() {
  fadeToBlackBy(strip, LED_COUNT, 1);
  uint8_t beatsin_idx = beatsin8(13, 0, LED_COUNT, 0, BEATSIN_PHASE);
  if (beatsin_idx < startup_idx)
    strip[beatsin_idx] = PRIMARY_HSV;
}

void patt_beatsin8_four() {
  fadeToBlackBy(strip, LED_COUNT, 1);
  uint8_t beatsin_idx_1 =
      beatsin8(13, 0, LED_COUNT, 0, BEATSIN_PHASE + BEATSIN_INCREMENT * 0);
  uint8_t beatsin_idx_2 =
      beatsin8(13, 0, LED_COUNT, 0, BEATSIN_PHASE + BEATSIN_INCREMENT * 1);
  uint8_t beatsin_idx_3 =
      beatsin8(13, 0, LED_COUNT, 0, BEATSIN_PHASE + BEATSIN_INCREMENT * 2);
  uint8_t beatsin_idx_4 =
      beatsin8(13, 0, LED_COUNT, 0, BEATSIN_PHASE + BEATSIN_INCREMENT * 3);

  if (beatsin_idx_1 < startup_idx)
    strip[beatsin_idx_1] = PRIMARY_HSV;
  if (beatsin_idx_2 < startup_idx)
    strip[beatsin_idx_2] = PRIMARY_HSV;
  if (beatsin_idx_3 < startup_idx)
    strip[beatsin_idx_3] = PRIMARY_HSV;
  if (beatsin_idx_4 < startup_idx)
    strip[beatsin_idx_4] = PRIMARY_HSV;
}

void patt_fade() {
  fill_solid(strip, startup_idx,
             hsv2rgb_spectrum(
                 CHSV(PRIMARY_SPEC_HUE, 255, ease8InOutQuad(fade_progress))));
  EVERY_N_MILLIS(FADE_UPDATE_TIME) {
    if (!fade_rev) {
      if (fade_progress - 1 > 0) {
        fade_progress--;
      } else if (fade_progress == 0) {
        fade_rev = true;
      } else {
        fade_progress = 0;
      }
    } else {
      if (fade_progress + 1 < 255) {
        fade_progress++;
      } else if (fade_progress == 255) {
        fade_rev = false;
      } else {
        fade_progress = 255;
      }
    }
  }
}

// based on neopixel sound reactive pendant and
// https://github.com/atuline/FastLED-SoundReactive
void patt_sound() {
  uint8_t i;
  uint16_t minLvl, maxLvl;
  int measured, height;

  measured = analogRead(MIC_PIN);                    // Raw reading from mic
  measured = abs(measured - 2048 - SOUND_DC_OFFSET); // Center on zero

  measured = (measured <= SOUND_NOISE)
                 ? 0
                 : (measured - SOUND_NOISE); // Remove noise/hum
  sound_lvl = ((sound_lvl * 7) + measured) >>
              3; // "Dampened" reading (else looks twitchy)

  // Calculate bar height based on dynamic min/max levels (fixed point):
  height = SOUND_TOP * (sound_lvl - sound_min_lvl_avg) /
           (long)(sound_max_lvl_avg - sound_min_lvl_avg);

  if (height < 0L)
    height = 0; // Clip output
  else if (height > SOUND_TOP)
    height = SOUND_TOP;
  if (height > sound_peak) {
    sound_peak = height; // Keep 'sound_peak' dot at top
    peak_millis = millis();
  }

  // Color pixels based on rainbow gradient
  for (i = 0; i < LED_COUNT; i++) {
    if (i < height)
      strip[i] = PRIMARY_HSV;
  }

  fadeToBlackBy(strip, LED_COUNT, 10);
  // only fade measurement to black but not peak (WIP, not working)
  //    if ((sound_peak - 1) - 0 >= 1) {
  //        fadeToBlackBy(strip(0,sound_peak - 1),sound_peak - 1,10);
  //        if (sound_peak + 1 <= LED_COUNT - 1) {
  //            fadeToBlackBy(strip(sound_peak + 1, LED_COUNT - 1),(LED_COUNT)
  //            - (sound_peak + 1),255);
  //        }
  //    }
  //    fadeToBlackBy(strip(0,sound_peak-2),sound_peak - 2,10);

  // Draw sound_peak dot
  if (sound_peak <= LED_COUNT - 1)
    strip[sound_peak] = CRGB::White;

  // after no peak for a while, Every few frames, make the sound_peak pixel
  // drop by 1:
  if (millis() - peak_millis > SOUND_PEAK_TIMEOUT) {
    if (++sound_dot_count >= SOUND_PEAK_FALL) {
      // fall rate
      if (sound_peak > 0)
        sound_peak--;
      sound_dot_count = 0;
    }
  }

  sound_vol[sound_vol_count] = measured; // Save sample for dynamic leveling
  if (++sound_vol_count >= SOUND_SAMPLES)
    sound_vol_count = 0; // Advance/rollover sample counter

  // Get volume range of prior frames
  minLvl = maxLvl = sound_vol[0];
  for (i = 1; i < SOUND_SAMPLES; i++) {
    if (sound_vol[i] < minLvl)
      minLvl = sound_vol[i];
    else if (sound_vol[i] > maxLvl)
      maxLvl = sound_vol[i];
  }
  // minLvl and maxLvl indicate the volume range over prior frames, used
  // for vertically scaling the output graph (so it looks interesting
  // regardless of volume level).  If they're too close together though
  // (e.g. at very low volume levels) the graph becomes super coarse
  // and 'jumpy'...so keep some minimum distance between them (this
  // also lets the graph go to zero when no sound is playing):
  if ((maxLvl - minLvl) < SOUND_TOP)
    maxLvl = minLvl + SOUND_TOP;
  sound_min_lvl_avg =
      (sound_min_lvl_avg * 63 + minLvl) >> 6; // Dampen min/max levels
  sound_max_lvl_avg =
      (sound_max_lvl_avg * 63 + maxLvl) >> 6; // (fake rolling average)
}

void patt_rainbow() {
  EVERY_N_MILLIS(RAINBOW_UPDATE_TIME) rainbow_hue--;
  fl::fill_rainbow_circular(strip, LED_COUNT, rainbow_hue, false);
}

void patt_rainbow_dual() {
  EVERY_N_MILLIS(RAINBOW_DUAL_UPDATE_TIME) rainbow_hue--;
  fl::fill_rainbow_circular(strip(0, LED_COUNT / 2), LED_COUNT / 2, rainbow_hue,
                            true);
  fl::fill_rainbow_circular(strip(LED_COUNT / 2 + 1, LED_COUNT), LED_COUNT / 2,
                            rainbow_hue, false);
}

// from fastled demo
void patt_confetti() {
  confetti_hue++;
  fadeToBlackBy(strip, LED_COUNT, 5);
  int pos = random16(LED_COUNT);
  strip[pos] += CHSV(confetti_hue + random8(64), 200, 255);
}
