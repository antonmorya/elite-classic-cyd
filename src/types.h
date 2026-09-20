#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

enum ColorMode { CYAN, PURPLE, GOLD, WHITE, NUM_MODES };

struct Point3D {
    float x, y, z;
};

struct Point2D {
    int x, y;
    float z; // For Z-sorting and shading
    bool valid;
};

struct Triangle {
    int v[3]; // Indices of vertices
    float avgZ;
    bool visible;
};

struct Particle {
    float x, y;
    float speed;
    uint8_t layer;      // 0 = far, 1 = mid, 2 = near
    uint8_t brightness; // far only: varies per-dot; mid/near use a fixed color
    // Motion mode, fixed at spawn and never changed mid-flight (only the
    // next spawn picks a fresh one) — see spawnParticle() in main.cpp.
    // 0 = linear: shared heading (hdx,hdy).
    // 1 = radial: own fixed (dirx,diry), bursting outward from an anchor
    //     point (screen center when approaching, the nose-direction "zone"
    //     when receding) until it leaves the layer's box.
    uint8_t mode;
    float dirx, diry; // used only when mode != 0
    // far/mid only: coin-flip at spawn between the layer's normal color and
    // plain white, so the layer reads as a random mix of both rather than
    // one uniform shade.
    bool useWhite;
};

#endif
