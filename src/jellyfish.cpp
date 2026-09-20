#include "jellyfish.h"

uint16_t getJellyfishColor(ColorMode mode, float brightness) {
    uint8_t r = 0, g = 0, b = 0;
    switch(mode) {
    case PURPLE:
        r = 144;
        g = 28;
        b = 230;
        break;
    case GOLD:
        r = 230;
        g = 194;
        b = 0;
        break;
    case WHITE:
        r = 230;
        g = 230;
        b = 230;
        break;
    case CYAN:
    default:
        r = 0;
        g = 230;
        b = 230;
        break;
    }

    r = (uint8_t)(r * brightness);
    g = (uint8_t)(g * brightness);
    b = (uint8_t)(b * brightness);

    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
