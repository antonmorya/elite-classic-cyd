#ifndef JELLYFISH_H
#define JELLYFISH_H

#include "config.h"
#include "types.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

uint16_t getJellyfishColor(ColorMode mode, float brightness = 1.0f);

#endif
