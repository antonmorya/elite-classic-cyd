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
    // cue. Three depth layers (far/mid/near), split evenly: far dots vary in
    // brightness (a speckled background, barely creeping); mid is a single
    // fixed dim gray, noticeably faster; near is white and fast — for
    // mid/near, speed carries the distinction, not color.
    for (int i = 0; i < NUM_PARTICLES; i++)
    {
        particles[i].x = random(0, SCREEN_WIDTH);
        particles[i].y = random(0, SCREEN_HEIGHT);
        particles[i].layer = i % 3;
        particles[i].brightness = random(50, 170); // used by the far layer only
        switch (particles[i].layer) {
            case 0: particles[i].speed = random(1, 4) / 10.0f; break;    // far: 0.1 - 0.3
            case 1: particles[i].speed = random(6, 12) / 10.0f; break;   // mid: 0.6 - 1.1
            default: particles[i].speed = random(15, 28) / 10.0f; break; // near: 1.5 - 2.7
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
    angle_y += rotation_speed;

    // Ship stays centered and only rotates — global_x/y/z_offset stay at
    // their 0 default (screen drift and camera zoom motion are disabled).

    // Recompute rotation first — we reuse it to find where the nose points.
    updateRotationParams(angle_x, angle_y, angle_z);

    // Flow direction = where the ship's nose actually points on screen right
    // now (rotated nose vector, screen-space x/y — the perspective divide
    // doesn't change its direction, only its length, so it can be skipped).
    // ONE shared direction for all streaks, so they read as the ship's
    // heading/motion.
    //
    // (Driving this straight from angle_y instead was tried — it rotates at
    // a perfectly uniform rate, but that rate has nothing to do with which
    // way the nose is actually facing once the fixed pitch tilt is factored
    // in, so the streaks pointed off at an unrelated angle — even
    // perpendicular to the ship's visible facing. The uneven rate you get
    // from the real nose vector — slow for a while, then a fast swing — is
    // not a bug, it's what the nose's screen position actually does under a
    // fixed-pitch spin; matching it is what keeps the streaks correct.)
    Point3D nose_r = rotateFast(Point3D{ 32.0f, -1.0f, 58.0f }); // cobra_vertices[0], recentred
    float hdx = -nose_r.x;   // travel direction: nose -> rear
    float hdy = -nose_r.y;
    float hlen = sqrtf(hdx * hdx + hdy * hdy);
    if (hlen < 1e-3f) { hdx = 0.0f; hdy = 1.0f; }   // nose aimed straight at camera
    else { hdx /= hlen; hdy /= hlen; }

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
            // All streaks share the exact same direction — the heading —
            // so they read unambiguously as ONE motion vector, not a hint
            // of one buried in per-streak noise.
            float spd = particles[i].speed * flow_scale;
            particles[i].x += hdx * spd;
            particles[i].y += hdy * spd;

            // Toroidal wrap: re-enter from the opposite edge.
            if (particles[i].x < 0) particles[i].x += SCREEN_WIDTH;
            else if (particles[i].x >= SCREEN_WIDTH) particles[i].x -= SCREEN_WIDTH;
            if (particles[i].y < 0) particles[i].y += SCREEN_HEIGHT;
            else if (particles[i].y >= SCREEN_HEIGHT) particles[i].y -= SCREEN_HEIGHT;

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
            canvas.setCursor(5, SCREEN_HEIGHT - 120);
            canvas.printf("HDG: %.0fdeg", atan2f(hdy, hdx) * 180.0f / PI);
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
