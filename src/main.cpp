#include "config.h"
#include "input_handler.h"
#include "jellyfish.h"
#include "cobra.h"
#include "math_3d.h"
#include "types.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>

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

// Testing toggle: false skips updating/drawing near streaks entirely, while
// the radial (axial-view) behavior is tuned for far/mid only. Flip back to
// true once near rejoins the radial redesign.
const bool SHOW_NEAR = false;

// Testing toggle: true gives every particle its own fixed color by array
// index instead of the normal layer-based scheme. A single screen dump is a
// still frame with no motion in it — two dumps a beat apart, with each dot
// individually identifiable, let a direction be read off from how far (and
// which way) each SPECIFIC dot moved between them. Flip back to false once
// streak behavior settles.
const bool DEBUG_UNIQUE_COLORS = true;

// Tuning knob: multiplies the far layer's base speed range (0.2-0.6
// px/frame at 1.0x). Play with this to slow down / speed up just the far
// background layer.
const float FAR_SPEED_SCALE = 0.05f;

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

// Below this axis_t (broadside-ness: 1 = fully broadside, 0 = nose
// dead-on/dead-away), new spawns use radial motion instead of linear.
const float AXIS_RADIAL_THRESHOLD = 0.5f;

// Assigns this particle a fresh position and motion mode, based on the
// CURRENT ship orientation at the moment of spawn — the mode then stays
// fixed for the particle's whole life (see Particle::mode in types.h).
// Deciding only at spawn, never live, is what keeps the radial case from
// feeding back on itself the way a live per-frame recompute did.
//
// (zoneOffX, zoneOffY) is only used for radial spawns: it offsets the burst
// point away from the box's true center, toward wherever the ship's nose is
// currently pointing on screen (zero when the view is purely axial, nose
// dead-on or dead-away — see loop()). That's "the zone the ship is flying
// into" — receding streaks should burst from there, not from a fixed,
// tilt-blind center point.
static void spawnParticle(Particle &p, bool radial, float radialSign, float hdx, float hdy,
                           float zoneOffX, float zoneOffY)
{
    int loX, hiX, loY, hiY;
    if (p.layer == 2) {
        loX = NEAR_MARGIN_X; hiX = SCREEN_WIDTH - NEAR_MARGIN_X;
        loY = NEAR_MARGIN_Y; hiY = SCREEN_HEIGHT - NEAR_MARGIN_Y;
    } else {
        loX = 0; hiX = SCREEN_WIDTH; loY = 0; hiY = SCREEN_HEIGHT;
    }

    if (!radial) {
        p.mode = 0;
        if (p.layer == 2) spawnNearOnEdge(p, hdx, hdy);
        else { p.x = random(loX, hiX); p.y = random(loY, hiY); }
        return;
    }

    // Radial: scattered anywhere in the box (not clustered at the anchor —
    // spawning a whole layer at one exact point every time reads as a
    // "flare", not a starfield), each already mid-flight: its direction is
    // fixed once, as the vector from the anchor through its own spawn
    // point, so it reads as having traveled there FROM the anchor in a
    // straight line. Despawns on leaving the box (see loop()). Approaching
    // anchors on the box's true center; receding anchors on the offset
    // "zone" above.
    p.mode = 1;
    float cx = (loX + hiX) / 2.0f, cy = (loY + hiY) / 2.0f;
    float ax = (radialSign > 0.0f) ? cx : cx + zoneOffX;
    float ay = (radialSign > 0.0f) ? cy : cy + zoneOffY;

    p.x = random(loX, hiX);
    p.y = random(loY, hiY);

    float ddx = p.x - ax, ddy = p.y - ay;
    float dlen = sqrtf(ddx * ddx + ddy * ddy);
    if (dlen < 1e-3f) {
        float angle = random(0, 6283) / 1000.0f; // 0 .. 2*PI
        p.dirx = cosf(angle); p.diry = sinf(angle);
    } else {
        p.dirx = ddx / dlen; p.diry = ddy / dlen;
    }
}

// A distinct, high-saturation color per particle array index (see
// DEBUG_UNIQUE_COLORS) — a full hue rotation stepped by a large, non-round
// angle so consecutive indices land far apart on the color wheel.
static uint16_t debugColorForIndex(int i)
{
    float hue = fmodf(i * 47.0f, 360.0f);
    float x = 1.0f - fabsf(fmodf(hue / 60.0f, 2.0f) - 1.0f);
    float r, g, b;
    if (hue < 60)       { r = 1; g = x; b = 0; }
    else if (hue < 120) { r = x; g = 1; b = 0; }
    else if (hue < 180) { r = 0; g = 1; b = x; }
    else if (hue < 240) { r = 0; g = x; b = 1; }
    else if (hue < 300) { r = x; g = 0; b = 1; }
    else                { r = 1; g = 0; b = x; }
    return tft.color565((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
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
        spawnParticle(particles[i], false, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f); // linear; no flow direction yet at boot
        particles[i].brightness = random(50, 170); // used by the far layer only
        switch (particles[i].layer) {
            case 0: particles[i].speed = (random(2, 7) / 10.0f) * FAR_SPEED_SCALE; break; // far: 0.2-0.6 * FAR_SPEED_SCALE
            case 1: particles[i].speed = random(11, 20) / 10.0f; break;  // mid: 1.1 - 2.0
            default: particles[i].speed = random(26, 45) / 10.0f; break; // near: 2.6 - 4.5
        }
    }
}

// Debug aid: handles single-byte commands sent over Serial from a host
// script, so the whole demo can be driven and inspected without touching
// the physical screen or taking a photo of it.
//   'D' -> dumps the current canvas framebuffer (8bpp RGB332,
//          SCREEN_WIDTH x SCREEN_HEIGHT) as a raw byte stream prefixed by a
//          small header, so a host script can reconstruct a PNG.
//   'A' -> reads 8 more bytes (two little-endian float32: angle_x, angle_y)
//          and applies them directly, bypassing touch — lets ship
//          orientation be driven programmatically (e.g. to sweep through
//          headings and dump a screenshot at each) instead of needing live
//          human touch input for every test.
static void handleSerialCommands()
{
    if (Serial.available() <= 0) return;
    int cmd = Serial.read();

    if (cmd == 'D' && canvas.created())
    {
        void *buf = canvas.getPointer();
        uint16_t w = SCREEN_WIDTH, h = SCREEN_HEIGHT;
        Serial.write((const uint8_t *)"FBV1", 4);
        Serial.write((const uint8_t *)&w, 2);
        Serial.write((const uint8_t *)&h, 2);
        Serial.write((const uint8_t *)buf, (size_t)w * h);
        Serial.flush();
    }
    else if (cmd == 'A')
    {
        uint8_t buf[8];
        if (Serial.readBytes(buf, 8) == 8)
        {
            memcpy(&angle_x, buf, 4);
            memcpy(&angle_y, buf + 4, 4);
        }
    }
    else if (cmd == 'P')
    {
        // Text dump of every particle's true internal state — direct and
        // unambiguous, unlike matching same-ish colored dots between two
        // screen dumps (which collides badly once there are 20+ of them).
        for (int i = 0; i < NUM_PARTICLES; i++)
        {
            Serial.printf("%d\tlayer=%d\tmode=%d\tx=%.3f\ty=%.3f\tspeed=%.4f\n",
                          i, particles[i].layer, particles[i].mode,
                          particles[i].x, particles[i].y, particles[i].speed);
        }
        Serial.println("END");
        Serial.flush();
    }
}

void loop()
{
    handleSerialCommands();

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
    // it's skipped) — used by every particle still in linear mode. When the
    // nose points near-straight at or away from the camera, "left-right on
    // screen" stops meaning anything (in_plane_len collapses towards 0);
    // axis_t below measures how broadside-vs-axial the view currently is,
    // and drives which mode NEW spawns pick (see spawnParticle) — existing
    // particles keep whatever mode they already have until their own next
    // respawn, so the whole field turns over gradually instead of every
    // streak snapping to a new behavior on the same frame.
    // The ship's actual nose is the EDGE between vertices 0 and 1 (left/
    // right nose points, cobra_vertices in cobra.cpp), not either one alone
    // — using vertex 0 by itself (tried first) put a permanent ~32-unit
    // leftward bias into every direction/zone computation below, since that
    // vertex sits off to one side of the ship's own symmetry axis. Their
    // midpoint (0, 1, 58) sits exactly ON that axis instead.
    Point3D nose_r = rotateFast(Point3D{ 0.0f, 1.0f, 58.0f }); // midpoint of cobra_vertices[0] and [1]
    float raw_x = -nose_r.x;   // travel direction: nose -> tail
    float raw_y = -nose_r.y;
    float in_plane_len = sqrtf(raw_x * raw_x + raw_y * raw_y);
    float total_len = sqrtf(raw_x * raw_x + raw_y * raw_y + nose_r.z * nose_r.z);
    float hdx, hdy;
    if (in_plane_len < 1e-3f) { hdx = 0.0f; hdy = 1.0f; }   // nose aimed straight at camera
    else { hdx = raw_x / in_plane_len; hdy = raw_y / in_plane_len; }
    float axis_t = (total_len > 1e-3f) ? (in_plane_len / total_len) : 1.0f;
    bool radialMode = axis_t < AXIS_RADIAL_THRESHOLD;
    // nose_r.z < 0 means the nose is the closest point to the camera (see
    // project()'s world_z = camera_dist + z), i.e. the ship is closing —
    // new radial spawns burst outward from screen center, like approaching
    // warp speed.
    float radial_sign = (nose_r.z < 0.0f) ? 1.0f : -1.0f;

    // "The zone the ship is flying into" for the receding case: the true
    // vanishing point of the nose's direction of travel — where its 3D ray,
    // extended to infinity, would converge on screen. (Projecting the nose
    // vertex ITSELF, tried first, barely moved: it's only 32 units off the
    // ship's own rotation axis, so at this camera distance its screen
    // offset from center tops out around 10px, unnoticeably small — the
    // zone needs the ray's direction, not that one nearby point on it.)
    // Taking the projection formula's limit as distance along the ray
    // approaches infinity cancels the distance term and leaves this closed
    // form; naturally 0 (dead center) exactly when nose_r.z is 0, i.e. the
    // travel direction is pure broadside with no "ahead" component at all.
    float fov = getFov();
    float zoneOffX = (fabsf(nose_r.z) > 1e-3f) ? (nose_r.x / nose_r.z) * fov : 0.0f;
    float zoneOffY = (fabsf(nose_r.z) > 1e-3f) ? (nose_r.y / nose_r.z) * fov : 0.0f;
    const float ZONE_MAX = SCREEN_WIDTH * 0.3f; // safety clamp for extreme close-ups
    if (zoneOffX > ZONE_MAX) zoneOffX = ZONE_MAX; else if (zoneOffX < -ZONE_MAX) zoneOffX = -ZONE_MAX;
    if (zoneOffY > ZONE_MAX) zoneOffY = ZONE_MAX; else if (zoneOffY < -ZONE_MAX) zoneOffY = -ZONE_MAX;

    // Testing aid: normally a mode change only affects NEW spawns, so the
    // field turns over gradually (see the flow-direction comment above) —
    // for far/mid specifically, whose box is the whole screen at a slow
    // speed, that gradual turnover can take tens of seconds. Force every
    // particle to respawn the instant the mode flips, so a test rotation
    // shows the new behavior immediately instead of after a long wait.
    // FORCE_RESPAWN_ON_MODE_CHANGE should go back to false once streak
    // behavior settles and the gradual turnover is wanted again.
    const bool FORCE_RESPAWN_ON_MODE_CHANGE = true;
    static int prevRadialState = -1; // -1 = unset, forces a respawn matching
                                      // reality on the very first frame too
    int curRadialState = radialMode ? 1 : 0;
    bool modeJustChanged = (curRadialState != prevRadialState);

    // A radial particle's burst direction is a random angle chosen once at
    // spawn (see spawnParticle) — it was never "aimed" at anything, so
    // there's no live direction to correct as the ship keeps turning. What
    // CAN go stale is the anchor it bursts from: as the ship keeps
    // reorienting, the zone moves, but particles already in flight keep
    // radiating from wherever they started, which can drift noticeably out
    // of step with where the ship is pointing now. Re-anchoring the whole
    // radial field once the zone has moved far enough fixes that — still no
    // live per-particle recompute, just the same respawn-all this already
    // does on a mode flip, triggered a bit more often.
    const float ZONE_DRIFT_RESPAWN = 20.0f; // px
    static float lastRespawnZoneX = 0.0f, lastRespawnZoneY = 0.0f;
    float zoneDx = zoneOffX - lastRespawnZoneX, zoneDy = zoneOffY - lastRespawnZoneY;
    bool zoneDrifted = radialMode && sqrtf(zoneDx * zoneDx + zoneDy * zoneDy) > ZONE_DRIFT_RESPAWN;

    if ((FORCE_RESPAWN_ON_MODE_CHANGE && modeJustChanged) || zoneDrifted) {
        for (int i = 0; i < NUM_PARTICLES; i++)
            spawnParticle(particles[i], radialMode, radial_sign, hdx, hdy, zoneOffX, zoneOffY);
        lastRespawnZoneX = zoneOffX;
        lastRespawnZoneY = zoneOffY;
    }
    prevRadialState = curRadialState;

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
            if (!SHOW_NEAR && particles[i].layer == 2) continue;

            // mode 0 (linear) uses the shared heading; radial mode flies
            // its own fixed direction, set once at spawn (see
            // spawnParticle) and never touched again until the next one.
            float pdx = (particles[i].mode == 0) ? hdx : particles[i].dirx;
            float pdy = (particles[i].mode == 0) ? hdy : particles[i].diry;
            float spd = particles[i].speed * flow_scale;
            particles[i].x += pdx * spd;
            particles[i].y += pdy * spd;

            int loX, hiX, loY, hiY;
            if (particles[i].layer == 2) {
                loX = NEAR_MARGIN_X; hiX = SCREEN_WIDTH - NEAR_MARGIN_X;
                loY = NEAR_MARGIN_Y; hiY = SCREEN_HEIGHT - NEAR_MARGIN_Y;
            } else {
                loX = 0; hiX = SCREEN_WIDTH; loY = 0; hiY = SCREEN_HEIGHT;
            }

            // Both modes despawn the same way: leaving the layer's box (see
            // spawnParticle/spawnNearOnEdge for why near respawns on its
            // rectangle's edge specifically).
            if (particles[i].x < loX || particles[i].x >= hiX ||
                particles[i].y < loY || particles[i].y >= hiY)
                spawnParticle(particles[i], radialMode, radial_sign, hdx, hdy, zoneOffX, zoneOffY);

            int hx = (int)particles[i].x;
            int hy = (int)particles[i].y;

            if (DEBUG_UNIQUE_COLORS && particles[i].layer != 0) {
                // Bigger and uniformly sized so every particle stays easy
                // to pick out and click identify in a screen dump. Far is
                // exempted here as its real look is being reviewed now —
                // the rest join back in one layer at a time.
                canvas.fillRect(hx - 1, hy - 1, 2, 2, debugColorForIndex(i));
            } else if (particles[i].layer == 2) {
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
            // Mark the current radial anchor point (where new radial spawns
            // would burst from right now), so it can be compared directly
            // against where streaks actually appear.
            if (radialMode) {
                float ax = SCREEN_WIDTH / 2.0f + (radial_sign > 0.0f ? 0.0f : zoneOffX);
                float ay = SCREEN_HEIGHT / 2.0f + (radial_sign > 0.0f ? 0.0f : zoneOffY);
                canvas.drawLine((int)ax - 6, (int)ay, (int)ax + 6, (int)ay, TFT_RED);
                canvas.drawLine((int)ax, (int)ay - 6, (int)ax, (int)ay + 6, TFT_RED);
            }

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
            canvas.printf("AXIS_T: %.2f %s", axis_t, radialMode ? "RADIAL" : "linear");
            canvas.setCursor(5, SCREEN_HEIGHT - 130);
            canvas.printf("ZONE: %.0f,%.0f", zoneOffX, zoneOffY);
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
