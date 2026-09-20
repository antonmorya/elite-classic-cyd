#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Screen dimensions
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// Colors
#define CL_BG 0x0000 // TFT_BLACK

// Pins
const int BTN_PIN = 0; // BOOT button

// Touch Screen configuration
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Backlight
#define TFT_BL_PIN 21

// Touch Calibration values
const uint16_t TOUCH_MIN_X = 200;
const uint16_t TOUCH_MAX_X = 3800;
const uint16_t TOUCH_MIN_Y = 250;
const uint16_t TOUCH_MAX_Y = 3850;

// Geometry configuration
const int BELL_RINGS = 4;
const int BELL_POINTS_PER_RING = 12;
const int NUM_TENTACLES = 12;
const int TENTACLE_SEGMENTS = 8;
const int NUM_BELL_VERTICES = BELL_RINGS * BELL_POINTS_PER_RING + 1;

// ============================================================================
// PARTICLE TUNING — COUNTS (this file). SPEED and other behavior knobs
// (FAR_SPEED_SCALE, MID_SPEED_SCALE, AUTO_ROTATE, SHOW_NEAR,
// DEBUG_UNIQUE_COLORS, AXIS_RADIAL_THRESHOLD, ...) live near the top of
// main.cpp instead — they have to be, since they're used inside loop().
// Counts have to live HERE instead, because they size the particles[] array
// at compile time, and this header is what everything else includes.
// ============================================================================
// Base count per layer times its own scale knob. Tweak the *_COUNT_SCALE
// values to get more/fewer of a layer without recounting, or just edit
// *_BASE_COUNT directly like a plain number if you don't need the knob.
#define FAR_BASE_COUNT 30
#define FAR_COUNT_SCALE 1.0
#define MID_BASE_COUNT 15
#define MID_COUNT_SCALE 1.0
#define NEAR_BASE_COUNT 7
#define NEAR_COUNT_SCALE 1.0

#define NUM_FAR ((int)(FAR_BASE_COUNT * FAR_COUNT_SCALE))
#define NUM_MID ((int)(MID_BASE_COUNT * MID_COUNT_SCALE))
#define NUM_NEAR ((int)(NEAR_BASE_COUNT * NEAR_COUNT_SCALE))
#define NUM_PARTICLES (NUM_FAR + NUM_MID + NUM_NEAR)

// RGB LED Pins (Active Low)
#define LED_R_PIN 4
#define LED_G_PIN 16
#define LED_B_PIN 17

#endif
