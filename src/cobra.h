#ifndef COBRA_H
#define COBRA_H

#include "config.h"
#include "types.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

// Cobra Mk III (Elite 1984, VRML з Irmen/Elite-ships).
// wireframe = true  — лише ребра кольору `color`.
// wireframe = false — solid: чорні грані (opaque) + ребра кольору `color` зверху.
void drawCobra(TFT_eSprite &canvas, float ax, float ay, float az,
               float gx, float gy, float gz, uint16_t color, bool wireframe,
               int *faces_visible = nullptr);

#endif
