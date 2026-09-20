# Elite Classic CYD — Project Documentation

A fork of **Denki Kurage** (ja: 電気くらげ, "electric jellyfish") that replaces the original low-poly jellyfish animation with a rotating **Cobra Mk III** starship from *Elite* (1984). It runs on the "Cheap Yellow Display" (CYD), an ESP32 development board with a built-in 2.8" TFT screen, and serves as an interactive desktop ornament / ambient animation.

---

## 1. Purpose & Vision

- **Goal:** Recreate the iconic Elite starship cockpit view on a compact embedded display — a retro, low-poly 3D Cobra Mk III slowly rotating in space, surrounded by a field of motion streaks that convey the ship's heading and velocity.
- **Vibe:** Ambient, "watch it swim and chill" — the same philosophy as the original Denki Kurage, but with a spaceship instead of a jellyfish.
- **Interaction:** Touch-driven controls to change color modes, adjust the flow/water-current speed, toggle wireframe/solid rendering, and show a debug overlay.

---

## 2. Hardware Target

| Component | Specification |
| :--- | :--- |
| Board | **ESP32-2432S028** (CYD / "Cheap Yellow Display") |
| Screen | 2.8" TFT ILI9341, **240 × 320** pixels |
| Touch | XPT2046 resistive touchscreen controller |
| Backlight | GPIO 21 (PWM via `ledc`) |
| RGB LED | Active-low, GPIOs 4 (R), 16 (G), 17 (B) — turned off in firmware |
| Enclosure | Custom 3D-printed stand (OpenSCAD, `.scad` / `.3mf`) |

Two hardware variants are supported (see `platformio.ini`):
- **`cyd`** — RGB order `TFT_RGB`.
- **`cyd2usb`** — RGB order `TFT_BGR** + display inversion (`CYD_INVERT_DISPLAY`). The README notes the project is *currently* targeted at the CYD2USB (USB-C) variant.

---

## 3. Software Stack

| Layer | Choice |
| :--- | :--- |
| Framework | **Arduino** on ESP32 (via PlatformIO) |
| Build system | **PlatformIO**, custom platform pin: `pioarduino/platform-espressif32.git#55.03.37` |
| Display driver | **TFT_eSPI** (`^2.5.33`) |
| Touch driver | **XPT2046_Touchscreen** (Paul Stoffregen, from GitHub) |
| Persistence | `Preferences.h` (built-in ESP32 NVM for settings) |

### Build & Flash
```bash
# Build both environments
~/.platformio/penv/bin/pio run

# Upload (pio is not on PATH by default; use the full penv path)
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.wchusbserial110
```

---

## 4. Project Structure

```
elite-classic-cyd/
├── platformio.ini          # Build config, two envs (cyd, cyd2usb)
├── README.md               # User-facing docs (BOM, controls, flashing)
├── DIALOG.json             # Conversation log / scratch
├── LICENSE
├── assets/                 # Images, webflash manifest
├── enclosure/              # OpenSCAD stand design (.scad/.3mf), Makefile
├── include/                # Shared header files (optional)
├── lib/                    # Project-specific libraries (optional)
└── src/
    ├── main.cpp            # Render loop, animation state, particle/streak system
    ├── config.h            # Screen size, pins, touch calibration, constants
    ├── types.h             # Point3D, Point2D, Triangle, Particle structs + ColorMode
    ├── math_3d.h/.cpp      # rotateFast(), project() — rotation + perspective
    ├── cobra.h/.cpp        # Cobra Mk III geometry (vertices, edges, faces) + renderer
    ├── jellyfish.h/.cpp    # Original jellyfish module (retained for reference)
    └── input_handler.h/.cpp # Touch/button handling, settings persistence
├── test/                   # PlatformIO tests (optional)
└── webflash/               # Browser-based flasher (cyd, cyd2usb variants)
```

---

## 5. Core Modules

### 5.1 `math_3d.cpp` — Rotation & Projection
- **`rotateFast(Point3D)`** — applies pitch (X), yaw (Y), roll (Z) using precomputed sin/cos tables (`updateRotationParams`).
- **`project(Point3D, gx, gy, gz)`** — perspective projection:
  - `world_z = camera_dist + p.z + global_z`, with `camera_dist = 500`.
  - `scale = fov / world_z`, with `fov = 160`.
  - Screen mapping: `x = WIDTH/2 + (p.x+gx)*scale`, `y = HEIGHT/2 + (p.y+gy)*scale`.
  - **Y-axis is flipped** (`+y` goes down on screen).
  - Points behind the camera (`world_z < 20`) or off-screen are marked `valid = false`.

### 5.2 `cobra.cpp` — Cobra Mk III Geometry & Renderer
- **12 vertices** (`cobra_vertices[]`) defining the ship body, wings, canopy, and rear.
- **24 edges** (`cobra_edges[]`) for wireframe rendering.
- **20 triangular faces** (`cobra_faces[]`) from triangulating the VRML `IndexedFaceSet`.
- **Backface culling** via the signed area of each triangle's screen-projected vertices (`area < 0` → front-facing). No z-buffer — pure 2D cross-product, which the project notes is reliable for this model.
- **Painter's algorithm** — visible faces are sorted by average Z (far first) and filled opaque black (`CL_BG`) for solid mode.
- **Solid vs Wireframe:** In solid mode, an edge is drawn only if at least one of its two adjacent faces is front-facing (mirrors the original Elite's culling). Wireframe draws all edges.
- Scale factor `COBRA_SCALE = 1.5`.

### 5.3 `input_handler.cpp` — Touch & Persistence
- Uses XPT2046 touchscreen + BOOT button.
- **Touch zones** (see debug overlay labels):
  - Top/Bottom strip (left) → Move Up / Down.
  - Middle left/right → Rotate camera.
  - Center → Cycle color modes.
  - Top-right corner → Toggle Solid/Wireframe mode.
  - Bottom-right corner → Toggle debug info.
- **Vertical direction** (`getVerticalDir()`): `-1` = up, `1` = down, `0` = none — used to scale the streak flow.
- Settings (color mode, brightness, wireframe flag) persisted via `Preferences.h`.

### 5.4 `config.h` — Constants
- Screen: `240 × 320`. Background color `CL_BG = 0x0000` (black).
- `NUM_PARTICLES = 50`.
- Touch calibration bounds (`TOUCH_MIN/MAX_X/Y`).
- Backlight pin, RGB LED pins.

---

## 6. Animation & Visual Behavior (current state)

### Ship motion
- The Cobra **stays centered on screen** and only **rotates around its body Y-axis** (`angle_y += rotation_speed`).
- Autonomous yaw: target speed is randomized every 45 seconds (range ±0.015 rad/frame) and smoothly interpolated for a natural, drifting rotation.
- Screen drift (X/Y offsets) and camera zoom (Z offset) are **deliberately disabled** — the ship is anchored at center.

### Motion streaks ("particles")
- **50 streaks** distributed across the **entire screen** (not clustered around the ship).
- Each streak has its own `speed` and `brightness`, plus a **fixed angular offset** (`spread`) set once at init — this prevents the "dancing" trail ends seen when jitter was recomputed per-frame.
- **Shared flow direction:** All streaks travel along ONE vector derived from the ship's heading — the screen-space vector from the nose (vertex 0) to the ship center. As the ship yaws, the whole field of streaks slowly turns, reading as the ship's course/motion.
- **Toroidal wrap:** When a streak leaves one screen edge, it reappears from the opposite edge — giving continuous, evenly distributed flow.
- **Speed lines:** Each streak is drawn as a short trailing line (head + tail), with length proportional to speed.
- **Flow scale** is driven by `getVerticalDir()`: ×4 when moving "up", ×−2.5 when "down" (reverse), ×1 otherwise — letting the user control current speed/direction via touch.

### Rendering pipeline (per frame)
1. Clear background (`CL_BG`).
2. Update animation math + ship rotation.
3. Compute streak flow direction from ship heading; update & draw all streaks.
4. Render Cobra (solid or wireframe) with backface culling + painter's sort.
5. Draw debug overlay if enabled (touch-zone grid, FPS, mode, visible-face count, yaw/y-offset/brightness).
6. Push sprite to screen; update FPS counter.

---

## 7. Debug Overlay
Enabled by tapping the bottom-right corner. Shows:
- Touch-zone grid with labels (T, TR, ML, MC, MR, B, BR).
- Live stats: `FPS`, `MODE` (Wireframe/Solid), `FACES` (visible face count), plus yaw, y-offset, brightness.

---

## 8. Build Environments & Known Constraints
- **Two envs** (`cyd`, `cyd2usb`) differ only in RGB order and display inversion.
- PlatformIO is expected to be run via `~/.platformio/penv/bin/pio` (not on PATH).
- Upload requires an explicit `--upload-port`.
- The custom pioarduino ESP32 platform is pinned to a specific git revision (`#55.03.37`) for reproducibility.

---

## 9. Build Status
Both `cyd` and `cyd2usb` environments build successfully (~7.7% RAM, ~29% flash used). Firmware uploads cleanly to `/dev/cu.wchusbserial110`.

---

## 10. Future / Open Threads
- README still references the original jellyfish; should be updated to reflect the Cobra Mk III theme.
- Web flasher (`webflash/`) exists for browser-based flashing — could be extended to the new firmware.
- The jellyfish module is retained but unused by default; could be made selectable.
