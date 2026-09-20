#include "config.h"
#include "input_handler.h"
#include "jellyfish.h"
#include "cobra.h"
#include "math_3d.h"
#include "types.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif

// Global objects
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);
InputHandler input;

// Testing toggle: false freezes the ship's own slow autonomous yaw spin, so
// the view stays put while streak behavior is tuned. Flip back to true when
// done testing.
const bool AUTO_ROTATE = false;

// Global state
ColorMode current_mode = PURPLE;
float phase = 0;
float global_x_offset = 0.0f;
float global_y_offset = 0.0f;
float global_z_offset = 0.0f;
float angle_x = -0.5f, angle_y = 0.4f, angle_z = 0.0f;
float rotation_speed = 0.005f;
float target_rotation_speed = 0.005f;
unsigned long last_rotation_change = 0;
unsigned long last_frame_time = 0;
float current_fps = 0;

Particle particles[NUM_PARTICLES];

// Near streaks spawn/despawn inset from the true screen edges by this
// padding — a fifth of each dimension, applied separately per axis (the
// screen isn't square, so a single margin distorts one axis or the other).
const int NEAR_MARGIN_X = SCREEN_WIDTH / 5;
const int NEAR_MARGIN_Y = SCREEN_HEIGHT / 5;

// Places a near streak ON the perimeter of the inset rectangle (not
// anywhere inside it) — that boundary is both where these streaks spawn and
// where they despawn, so they're always seen crossing the ship's
// neighbourhood rather than popping in partway across it.
//
// (dx, dy) is the current flow direction; only an edge the flow actually
// enters through is eligible. Spawning on an edge it's about to exit
// through instead made the streak cross back out on the very next frame —
// a one-frame flicker right at that edge. Pass (0,0) when there's no flow
// direction yet (e.g. the very first spawn at boot) to allow any edge.
static void spawnNearOnEdge(Particle &p, float dx, float dy)
{
    int innerW = SCREEN_WIDTH - 2 * NEAR_MARGIN_X;
    int innerH = SCREEN_HEIGHT - 2 * NEAR_MARGIN_Y;

    bool canLeft = dx >= 0.0f, canRight = dx <= 0.0f;
    bool canTop = dy >= 0.0f, canBottom = dy <= 0.0f;
    if (dx == 0.0f && dy == 0.0f) canLeft = canRight = canTop = canBottom = true;

    int choices[4], n = 0;
    if (canLeft) choices[n++] = 0;
    if (canRight) choices[n++] = 1;
    if (canTop) choices[n++] = 2;
    if (canBottom) choices[n++] = 3;

    switch (choices[random(0, n)]) {
        case 0: // left
            p.x = NEAR_MARGIN_X;
            p.y = NEAR_MARGIN_Y + random(0, innerH);
            break;
        case 1: // right
            p.x = SCREEN_WIDTH - NEAR_MARGIN_X;
            p.y = NEAR_MARGIN_Y + random(0, innerH);
            break;
        case 2: // top
            p.x = NEAR_MARGIN_X + random(0, innerW);
            p.y = NEAR_MARGIN_Y;
            break;
        default: // bottom
            p.x = NEAR_MARGIN_X + random(0, innerW);
            p.y = SCREEN_HEIGHT - NEAR_MARGIN_Y;
            break;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("Denki Kurage - Initted");

    // Turn OFF RGB LED (Active Low)
    pinMode(LED_R_PIN, OUTPUT);
    pinMode(LED_G_PIN, OUTPUT);
    pinMode(LED_B_PIN, OUTPUT);
    digitalWrite(LED_R_PIN, HIGH);
    digitalWrite(LED_G_PIN, HIGH);
    digitalWrite(LED_B_PIN, HIGH);

    tft.init();
    tft.setRotation(0);
#ifdef CYD_INVERT_DISPLAY
    tft.invertDisplay(true);
#else
    tft.invertDisplay(false);
#endif
    tft.fillScreen(CL_BG);

    canvas.setColorDepth(8);
    if (!canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT))
    {
        Serial.println("Sprite FAILED");
    }

    input.begin();
    input.loadSettings(current_mode);

    // Initialize Backlight
    ledcAttach(TFT_BL_PIN, 5000, 8);
    ledcWrite(TFT_BL_PIN, input.getBrightness()); // Apply loaded brightness

    // Distribute streaks across the ENTIRE screen (not a disk around the ship).
    // Direction is NOT per-streak — all of them share the exact same heading
    // vector (set in loop()), so they read as one strictly parallel motion
    // cue. Three depth layers: far dots vary in brightness (a speckled
    // background, barely creeping); mid is a single fixed dim gray,
    // noticeably faster; near is white, fast, and few — a handful of close
    // streaks around the ship rather than a full layer.
    for (int i = 0; i < NUM_PARTICLES; i++)
    {
        particles[i].layer = (i < NUM_FAR) ? 0 : (i < NUM_FAR + NUM_MID) ? 1 : 2;
        // Near spawns ON the edge of its inset rectangle (see spawnNearOnEdge);
        // far/mid spawn anywhere and later wrap edge-to-edge, same as always.
        if (particles[i].layer == 2) {
            spawnNearOnEdge(particles[i], 0.0f, 0.0f); // no flow direction yet at boot
        } else {
            particles[i].x = random(0, SCREEN_WIDTH);
            particles[i].y = random(0, SCREEN_HEIGHT);
        }
        particles[i].brightness = random(50, 170); // used by the far layer only
        switch (particles[i].layer) {
            case 0: particles[i].speed = random(2, 7) / 10.0f; break;    // far: 0.2 - 0.6
            case 1: particles[i].speed = random(11, 20) / 10.0f; break;  // mid: 1.1 - 2.0
            default: particles[i].speed = random(26, 45) / 10.0f; break; // near: 2.6 - 4.5
        }
    }
}

void loop()
{
    // Input
    input.update(current_mode, angle_x, angle_y);
    ledcWrite(TFT_BL_PIN,
              input.getBrightness()); // Update brightness dynamically

    // Background
    if (canvas.created())
    {
        canvas.fillScreen(CL_BG);
    }

    // Animation Math
    phase += 0.08f + (random(-10, 11) / 1000.0f); // Add slight randomness
    if (phase > 2.0f * PI * 100.0f)
    {
        phase -= 2.0f * PI * 100.0f;
    }

    // Slow autonomous rotation around body axis (Y-axis)
    // Randomize target speed every 45 seconds
    if (millis() - last_rotation_change > 45000)
    {
        // Range: -0.015 to 0.015 radians per frame
        target_rotation_speed = (random(-150, 150) / 10000.0f);
        last_rotation_change = millis();
    }
    // Smoothly interpolate current speed to target
    rotation_speed += (target_rotation_speed - rotation_speed) * 0.005f;
    if (AUTO_ROTATE) angle_y += rotation_speed;

    // Ship stays centered and only rotates — global_x/y/z_offset stay at
    // their 0 default (screen drift and camera zoom motion are disabled).

    // Recompute rotation first — we reuse it to find where the nose points.
    updateRotationParams(angle_x, angle_y, angle_z);

    // Flow direction = the ship's nose-to-tail axis, projected to screen
    // (perspective divide only changes its length, not its direction, so
    // it's skipped). One shared direction for every streak in every layer —
    // a radial (toward/away-from-center) blend for the axial "nose at/away
    // from camera" case was tried and dropped: it made every layer's
    // motion visibly non-linear (each streak nudged by its own fixed
    // per-particle radial component), which is exactly what a "speed
    // direction" cue must not do.
    Point3D nose_r = rotateFast(Point3D{ -32.0f, 1.0f, 58.0f }); // cobra_vertices[0], recentred
    float raw_x = -nose_r.x;   // travel direction: nose -> tail
    float raw_y = -nose_r.y;
    float in_plane_len = sqrtf(raw_x * raw_x + raw_y * raw_y);
    float hdx, hdy;
    if (in_plane_len < 1e-3f) { hdx = 0.0f; hdy = 1.0f; }   // nose aimed straight at camera
    else { hdx = raw_x / in_plane_len; hdy = raw_y / in_plane_len; }

    int v_dir = input.getVerticalDir();
    const float flow_scale = (v_dir == -1) ? 4.0f : (v_dir == 1) ? -2.5f : 1.0f;

    // Authentic Elite starfield: plain dots, not streaked lines. Far is a
    // speckled 1x1 background of varying gray; mid is a fixed dim gray 1x1;
    // near is a fixed white 2x2 — the only layer still large enough to read
    // as "close". No orientation — a rotated 2x6 blip didn't read well.
    const uint16_t COLOR_DIM = tft.color565(110, 110, 110);
    const uint16_t COLOR_NEAR = tft.color565(255, 255, 255);

    if (canvas.created())
    {
        for (int i = 0; i < NUM_PARTICLES; i++)
        {
            // Every layer flies the same plain linear heading — no radial
            // blend for anyone now (see the flow-direction comment above).
            float spd = particles[i].speed * flow_scale;
            particles[i].x += hdx * spd;
            particles[i].y += hdy * spd;

            if (particles[i].layer == 2) {
                // Near: once past the inset rectangle, respawn ON its
                // perimeter (see spawnNearOnEdge) — never inside it. That
                // rectangle's edge is both the spawn and despawn boundary,
                // so a streak is always seen crossing the ship's
                // neighbourhood, never popping in partway across it.
                if (particles[i].x < NEAR_MARGIN_X || particles[i].x >= SCREEN_WIDTH - NEAR_MARGIN_X ||
                    particles[i].y < NEAR_MARGIN_Y || particles[i].y >= SCREEN_HEIGHT - NEAR_MARGIN_Y) {
                    spawnNearOnEdge(particles[i], hdx, hdy);
                }
            } else {
                // Far/mid: plain toroidal wrap, edge to edge — unchanged.
                if (particles[i].x < 0) particles[i].x += SCREEN_WIDTH;
                else if (particles[i].x >= SCREEN_WIDTH) particles[i].x -= SCREEN_WIDTH;
                if (particles[i].y < 0) particles[i].y += SCREEN_HEIGHT;
                else if (particles[i].y >= SCREEN_HEIGHT) particles[i].y -= SCREEN_HEIGHT;
            }

            int hx = (int)particles[i].x;
            int hy = (int)particles[i].y;

            if (particles[i].layer == 2) {
                canvas.fillRect(hx - 1, hy - 1, 2, 2, COLOR_NEAR);
            } else if (particles[i].layer == 1) {
                canvas.drawPixel(hx, hy, COLOR_DIM);
            } else {
                uint8_t v = particles[i].brightness;
                canvas.drawPixel(hx, hy, tft.color565(v, v, v));
            }
        }
    }

    // 3D Geometry — Cobra Mk III (wireframe, обчислюється всередині cobra.cpp)

    // Render
    if (canvas.created())
    {
        bool wireframe = input.isWireframeMode();
        int faces_visible = 0;
        drawCobra(canvas, angle_x, angle_y, angle_z,
                  global_x_offset, global_y_offset, global_z_offset,
                  getJellyfishColor(current_mode), wireframe, &faces_visible);

        // Debug Touch Zones (Dotted Lines for "thinner" look)
        if (input.isDebugMode())
        {
            uint16_t dbg_col = 0xFFE0; // TFT_YELLOW
            // Horizontal dividers (Full width)
            for (int x = 0; x < SCREEN_WIDTH; x += 4)
            {
                canvas.drawPixel(x, 45, dbg_col);
                canvas.drawPixel(x, SCREEN_HEIGHT - 45, dbg_col);
            }
            // Vertical dividers (Middle section)
            for (int y = 45; y < SCREEN_HEIGHT - 45; y += 4)
            {
                canvas.drawPixel(80, y, dbg_col);
                canvas.drawPixel(160, y, dbg_col);
            }

            // Corner Square Vertical Dividers (Only in top and bottom strips)
            for (int y = 0; y < 45; y += 4)
                canvas.drawPixel(SCREEN_WIDTH - 40, y, dbg_col);
            for (int y = SCREEN_HEIGHT - 45; y < SCREEN_HEIGHT; y += 4)
                canvas.drawPixel(SCREEN_WIDTH - 40, y, dbg_col);

            // Stats Text
            canvas.setTextColor(dbg_col);
            canvas.setTextSize(1);

            // Region Labels
            canvas.setTextDatum(MC_DATUM);
            canvas.drawString("T", 100, 22);
            canvas.drawString("TR", 220, 22);
            canvas.drawString("ML", 40, 160);
            canvas.drawString("MC", 120, 160);
            canvas.drawString("MR", 200, 160);
            canvas.drawString("B", 100, 297);
            canvas.drawString("BR", 220, 297);

            canvas.setTextDatum(TL_DATUM);
            canvas.setCursor(5, SCREEN_HEIGHT - 60);
            canvas.printf("FPS: %.1f", current_fps);
            canvas.setCursor(5, SCREEN_HEIGHT - 70);
            canvas.printf("MODE: %s", wireframe ? "Wireframe" : "Solid");
            canvas.setCursor(5, SCREEN_HEIGHT - 80);
            canvas.printf("FACES: %d", faces_visible);
            canvas.setCursor(5, SCREEN_HEIGHT - 90);
            canvas.printf("YAW: %.2f", angle_y);
            canvas.setCursor(5, SCREEN_HEIGHT - 100);
            canvas.printf("PITCH: %.2f", angle_x);
            canvas.setCursor(5, SCREEN_HEIGHT - 110);
            canvas.printf("BRI: %d", input.getBrightness());
        }

        canvas.pushSprite(0, 0);
    }

    // Update FPS
    unsigned long now = millis();
    if (now > last_frame_time)
    {
        current_fps = 1000.0f / (now - last_frame_time);
    }
    last_frame_time = now;

    delay(10);
}
