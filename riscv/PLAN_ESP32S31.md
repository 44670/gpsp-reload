# ESP32-S31-Korvo-1 Dependency-Free Display Driver and gpSP Integration Plan

## 0. Goal and Scope

This plan adds a direct display path for the `ESP32-S31-Korvo-1` to gpSP. It
follows the design used by `esp-walkie-talkie`: the project owns board
initialization, framebuffers, and frame submission, and it calls only built-in
ESP-IDF drivers. It does not add a board BSP, LVGL, or graphics components from
the Component Registry.

Here, "dependency-free" means no external or managed components beyond
ESP-IDF. It does not mean avoiding ESP-IDF itself. The RGB LCD peripheral is
still accessed through ESP-IDF's built-in `esp_lcd` driver.

The first milestone is limited to:

- Initializing the Korvo-1 800x480, 16-bit RGB LCD.
- Integer-scaling gpSP's 240x160 RGB565 output by 3x to 720x480, with 40-pixel
  black bars on the left and right.
- Using two PSRAM framebuffers and frame-complete notification to avoid tearing
  and prevent the CPU from overwriting a buffer being scanned by DMA.
- Preserving the semantics of the existing gpSP video callback, frame hash, and
  debug output.
- Allowing the emulator to keep running when the LCD is absent or initialization
  fails, so serial and headless debugging remain available.

The first milestone explicitly excludes LVGL, touch input, menus, cameras,
audio, an SD-card browser, PPA scaling, and a general multi-board BSP. None of
these may be a prerequisite for the first displayed frame.

---

## 1. Verified Reference Implementations

### 1.1 `esp-walkie-talkie`

Local reference repository:

- Path: `/home/john/work/CardPuterADV/esp-walkie-talkie`
- Git commit: `cae4aac87ed67a08c7bdf18f85e6c502408ef68b`
- Primary files:
  - `main/cores3se_board.cpp`
  - `main/cardputer_adv_board.cpp`
  - `main/cores3se_hw.hpp`
  - `main/CMakeLists.txt`

Design principles to retain:

1. Keep board constants, initialization order, and the display API in a
   dedicated driver, so the emulator core never knows about GPIOs, timings, or
   panel details.
2. Depend only on built-in ESP-IDF components; do not initialize hardware
   indirectly through M5, LovyanGFX, LVGL, or a board BSP.
3. Make framebuffer ownership explicit, prefer static or fixed-size storage,
   and perform no temporary heap allocation in the frame hot path.
4. Check each initialization step and return failures to the caller. Do not use
   `ESP_ERROR_CHECK()` to reboot the whole device for a degradable LCD error.
5. Keep the public API narrow: initialize, query readiness, and present an
   RGB565 frame.
6. Driver/ISR callbacks only notify a task or update counters. They never scale,
   copy, or log from interrupt context.

Parts that must not be copied directly: `esp-walkie-talkie` drives an SPI LCD
and byte-swaps RGB565 before transmission. Korvo-1 uses a continuously scanned
parallel RGB LCD, so it must not inherit SPI window commands, the RAMWR flow, or
in-place byte swapping.

### 1.2 Official Sources for Korvo-1 Hardware Facts

Use the board implementation shipped with the factory demo as the primary
reference:

- Repository: `/home/john/esp32-s31/software/esp-dev-kits`
- Git commit: `ce50c11e1cc09040b242de4bcc079b12cdb89fd4`
- Files:
  - `examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo/esp32_s31_korvo.c`
  - `examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo/include/bsp/display.h`
  - `examples/esp32-s31-korvo/examples/common_components/esp32_s31_korvo/include/bsp/esp32_s31_korvo.h`

Cross-check it against the second official implementation:

- Repository: `/home/john/esp32-s31/software/esp-bsp`
- Git commit: `74e3cd238fdd81cda4fd03184c32f6776c41b7cd`
- Files:
  - `bsp/esp32_s31_korvo_1/src/bsp_display.c`
  - `bsp/esp32_s31_korvo_1/include/bsp/esp32_s31_korvo_1.h`

Current ESP-IDF baseline:

- Path: `/home/john/esp-idf`
- Git commit: `055ba9d3f9c6fd9a0efacd4993a2a942972dd65d`
- RGB driver API: `components/esp_lcd/rgb/include/esp_lcd_panel_rgb.h`
- Double-buffer behavior reference: `examples/peripherals/lcd/rgb_panel/`

Do not add either BSP component as a project dependency. They provide
officially validated pins, panel timings, and API usage only. Reimplement the
actual gpSP driver as a minimal local module.

### 1.3 Existing gpSP Video Path

The existing ESP32-S3 port already defines the boundary to reuse:

- `esp32s3/cores3se_lcd.c`: direct board driver with an
  `init/ready/present` API.
- `esp32s3/main/app_main.c::video_cb()`: hashes the source frame, optionally
  captures it, and then calls LCD present.
- `common.h`: the visible GBA resolution is 240x160 and its pitch is 240.
- `libretro/libretro.c`: the video callback receives RGB565 data and an
  explicit pitch.

Keep the S31 implementation at the same layer. Do not put LCD logic in
`video.cc`, `libretro.c`, or a dynarec backend.

---

## 2. Fixed Hardware Configuration

### 2.1 RGB Data and Control Pins

| Signal | GPIO | RGB565 bits |
|---|---:|---|
| DATA0..4 | 8..12 | B3..B7 |
| DATA5..10 | 13..18 | G2..G7 |
| DATA11..15 | 19, 33..36 | R3..R7 |
| PCLK | 40 | Pixel clock |
| DE | 43 | Data enable |
| HSYNC | 44 | Horizontal sync |
| VSYNC | 45 | Vertical sync |

Initial configuration:

- `data_width = 16`
- `in_color_format = LCD_COLOR_FMT_RGB565`
- `h_res = 800`
- `v_res = 480`
- `pclk_active_neg = true`
- `dma_burst_size = 128`
- `num_fbs = 2`
- `fb_in_psram = true`

### 2.2 Panel-Timing Baseline and Known Disagreement

The factory demo uses:

```text
PCLK  = 26 MHz
HSYNC = pulse 1, back porch 40, front porch 20
VSYNC = pulse 1, back porch 10, front porch 5
```

This combination has a theoretical refresh rate of approximately 60.882 Hz,
making it the best initial baseline for gpSP's approximately 60 fps output.

Component Registry BSP `1.0.0~1` still uses a different configuration: 18 MHz,
horizontal `40/40/48`, and vertical `23/32/13`, for a theoretical refresh rate
of approximately 35.395 Hz. Keep it only as a named compatibility fallback, not
as the default.

Record the behavior of both profiles on hardware and remove the fallback only
after the actual panel revision is confirmed. Never mix parameters from the two
profiles.

### 2.3 DISP, Backlight, and SPI Control Lines

The factory demo sets `disp_gpio_num`, backlight, and reset to `GPIO_NUM_NC`.
The older BSP treats GPIO38 as RGB DISP, while schematics and newer board
headers also describe GPIO38 as LCD_CS or a multiplexed control signal.

Therefore:

- Default to `disp_gpio_num = GPIO_NUM_NC`; do not drive GPIO38 proactively.
- Do not treat `ESP_ERR_NOT_SUPPORTED` from
  `esp_lcd_panel_disp_on_off()` as an initialization failure.
- Do not offer software brightness control in the first version.
- Only add a separate `GPIO38_DISP` experimental profile if the default produces
  no image and an oscilloscope, schematic, or official code proves that it is
  required. Do not guess the level in the normal path.

---

## 3. Dependency Boundary

Allowed built-in ESP-IDF components:

- `esp_lcd`
- `esp_driver_gpio`
- `esp_psram`
- `esp_partition` for gpSP's existing ROM-partition path
- `esp_hw_support`
- FreeRTOS kernel APIs

Forbidden for the first display milestone:

- `espressif/esp32_s31_korvo_1`
- `esp_lvgl_port`, `esp_lvgl_adapter`, or `lvgl`
- `esp_lcd_touch_gt1151`
- The factory demo's common BSP component
- M5Unified, M5GFX, or LovyanGFX
- Any Component Registry package added only for frame presentation

Do not create `idf_component.yml` in the project root, and do not generate
`managed_components/`. A `dependencies.lock` file must not be required to build
the display driver. Dependency auditing is an acceptance criterion; inspecting
CMake files alone is insufficient.

---

## 4. Proposed File Layout and API

```text
esp32s31/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── PLAN.md
├── korvo1_lcd.c
├── korvo1_lcd.h
├── psram_static.c
├── psram_static.h
└── main/
    ├── CMakeLists.txt
    └── app_main.c
```

The pure-C scaling algorithm must be host-testable. If ESP-IDF headers make
`korvo1_lcd.c` inconvenient to compile on the host, split it into:

```text
esp32s31/rgb565_scale3x.c
esp32s31/rgb565_scale3x.h
tests/esp32s31/rgb565_scale3x_test.c
```

Keep the public LCD API narrow and do not expose the panel handle:

```c
typedef struct {
    uint32_t submitted_frames;
    uint32_t completed_frames;
    uint32_t dropped_frames;
    uint32_t wait_timeouts;
    uint32_t vsync_count;
    uint32_t last_scale_us;
    uint32_t max_scale_us;
} esp32s31_lcd_stats_t;

bool esp32s31_korvo1_lcd_init(void);
bool esp32s31_korvo1_lcd_ready(void);
bool esp32s31_korvo1_lcd_present_rgb565(const void *pixels,
                                         unsigned width,
                                         unsigned height,
                                         size_t pitch);
void esp32s31_korvo1_lcd_get_stats(esp32s31_lcd_stats_t *out);
```

Do not expose `esp_lcd_panel_handle_t` or PSRAM framebuffer pointers to the
emulator core. This keeps future changes to bounce buffers, PPA, or timing
profiles out of gpSP core code.

---

## 5. Initialization Design

Perform initialization in the following order, with explicit logging and error
return at every step:

1. Verify that the target is `CONFIG_IDF_TARGET_ESP32S31`.
2. Verify that PSRAM is enabled and available; record capacity, largest
   contiguous block, and current free space.
3. Build `esp_lcd_rgb_panel_config_t` from the fixed pins and 26 MHz timing in
   Section 2.
4. Set `num_fbs = 2`, `.flags.fb_in_psram = true`, and initially set
   `bounce_buffer_size_px = 0`.
5. Call `esp_lcd_new_rgb_panel()`.
6. Register `on_vsync` and `on_frame_buf_complete` callbacks. The callbacks only
   call `vTaskNotifyGiveFromISR()` and/or update counters.
7. Call `esp_lcd_panel_reset()` and `esp_lcd_panel_init()`.
8. Obtain the two driver-allocated framebuffers with
   `esp_lcd_rgb_panel_get_frame_buffer()`.
9. Clear both framebuffers to black and submit the first test pattern.
10. Mark the display ready only after receiving at least one VSYNC or
    frame-complete event.

Never call `esp_lcd_new_rgb_panel()`, allocate a framebuffer, or register
callbacks once per frame.

### 5.1 ISR Safety

- A callback must not access flash strings, call ordinary logging functions, or
  perform RGB scaling.
- If `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y` is enabled, mark the callback and its
  direct call chain `IRAM_ATTR`, and place callback context and counters in
  internal RAM.
- Framebuffers may reside in PSRAM, but an ISR must not write them directly.
- Every wait must have a timeout; lost LCD synchronization must not deadlock the
  emulator task.

### 5.2 Backlight Control Is Not a Success Condition

The current official Korvo-1 definition has no usable backlight GPIO.
Initialization succeeds when the RGB panel is created, DMA is running, and
VSYNC events arrive, not when a backlight API returns `ESP_OK`. If the physical
display requires separate power or an LCD-subboard jumper, document it as a
hardware prerequisite instead of inventing a software control pin.

---

## 6. Per-Frame Submission Design

### 6.1 Fixed Integer Scaling

The input must satisfy:

- `width == 240`
- `height == 160`
- `pitch >= 240 * sizeof(uint16_t)`
- RGB565 in native byte order

Output layout:

```text
800x480 panel
┌──────────────────────────────────────────────────────────────┐
│ 40 px black │ 240x160 source scaled 3x = 720x480 │ 40 px black │
└──────────────────────────────────────────────────────────────┘
```

Process one source row at a time:

1. Write every source pixel three times consecutively to produce one 720-pixel
   destination row.
2. Copy that destination row to the following two rows, or write all three
   destination rows together.
3. Write the left and right black bars only when initially clearing each
   framebuffer; ordinary frames update only the central 720x480 region.
4. Prefer aligned 32-bit or 64-bit stores, but retain a simple scalar
   implementation as the correctness oracle.

Do not reuse the in-place RGB565 byte swap from the CoreS3 SPI path. The
official Korvo BSP declares `BSP_LCD_BIGENDIAN=0`; verify color order with red,
green, and blue test patterns. If colors are wrong, first check RGB data-line
mapping and `LCD_COLOR_FMT_RGB565` instead of blindly swapping bytes.

### 6.2 Double-Buffer Ownership

Allow only these three buffer states:

- `FRONT_SCANOUT`: LCD/DMA is currently consuming the buffer; the CPU must not
  write it.
- `BACK_WRITABLE`: the CPU may scale the next frame into the buffer.
- `BACK_SUBMITTED`: a switch has been requested and the code is waiting for a
  frame-complete notification.

Recommended flow:

1. Wait until frame-complete has explicitly released the buffer to be written.
2. Perform 3x scaling into the back buffer.
3. Clear stale task notifications.
4. Call `esp_lcd_panel_draw_bitmap(panel, 0, 0, 800, 480, back_fb)` with the
   complete framebuffer pointer.
5. Update front/back indices. Before reusing the old front buffer, wait for
   frame-complete.

Follow the notification order used by the current ESP-IDF RGB double-buffer
example; do not infer it from intuition. The first version may synchronously
wait for frame-complete after submission to establish tear-free correctness.
Overlap scaling with scanout only after that path is proven.

### 6.3 Timeouts and Degradation

- Use a wait limit of two panel frame periods plus a fixed margin.
- On timeout, increment `wait_timeouts` and return false. The caller continues
  emulation rather than aborting.
- After a threshold of consecutive timeouts, mark the LCD unavailable and stop
  submitting frames to avoid logging every frame.
- Rate-limit logs to at most once per second and print metrics from a separate,
  low-frequency status path.

---

## 7. Memory and Bandwidth Budget

| Item | Size |
|---|---:|
| One 800x480 RGB565 framebuffer | 768,000 B |
| Two framebuffers | 1,536,000 B (approximately 1.465 MiB) |
| gpSP source framebuffer (240x161) | 77,280 B |
| 720x480 region updated per frame | 691,200 B |
| Optional two 20-line bounce buffers | 64,000 B internal RAM |

At 60 fps, the CPU writes approximately 41.47 MB/s to the scaled region alone.
A 26 MHz, 16-bit RGB scanout transfers approximately 52 MB/s, including clocks
during blanking. This excludes gpSP, JIT, audio, and cache-refill traffic, so
hardware measurements are mandatory; theoretical peak bandwidth is not enough.

Constraints:

- Do not allocate a third 800x480 staging framebuffer.
- Keep ownership of the 240x160 source buffer in the existing gpSP video path.
- Let `esp_lcd` allocate framebuffers once in PSRAM; perform no `malloc()` or
  `free()` in the hot path.
- Keep callback state, notification objects, and statistics in internal RAM.
- If LCD underruns occur, first verify timing and PSRAM configuration, then try
  fixed-height bounce buffers, and only then lower PCLK. Do not respond by
  adding an external graphics library.

---

## 8. gpSP Integration Point

The S31 `video_cb()` must preserve the existing ESP32-S3 order:

1. Compute the streaming FNV-1a hash over the original 240x160 frame.
2. If debug capture is enabled, copy the original frame for the serial PNG tool.
3. If the LCD is ready, call
   `esp32s31_korvo1_lcd_present_rgb565()`.
4. Increment gpSP's video-frame count whether or not LCD presentation succeeds.

This ensures that the display driver does not alter the framebuffer hash
observed by existing regression tests. A hash of scaled output may be added as
a diagnostic, but it must not replace the original GBA-frame hash.

Initialize the LCD before `retro_init()` and load a game only after the test
pattern succeeds. An initialization failure logs one explicit reason, after
which the debug/headless path remains usable.

Display submission must not know whether the CPU backend is the interpreter or
RV32IM dynarec. Executing that backend on S31 hardware is a separate gate;
static test patterns and host scaler tests must not be blocked by dynarec
progress.

---

## 9. Phased Implementation

### Phase 0: Freeze Facts and Resolve Hardware Ambiguity

- Record the three reference commits for walkie-talkie, esp-dev-kits, and
  ESP-IDF in this plan or driver comments.
- Preserve the 26 MHz and 18 MHz timings as named profiles, enabling only the
  26 MHz profile by default.
- Do not drive GPIO38 by default. Prepare an explicit experiment switch but do
  not autodetect it in ordinary builds.
- Verify that the physical board is Korvo-1 V1.1 with the 4.3-inch LCD subboard.

Gate: pins, timings, display-subboard revision, and power requirements all have
traceable records, with no unexplained "magic GPIO."

### Phase 1: Minimal ESP-IDF Project and Static Test Pattern

- Create the `esp32s31/` ESP-IDF app with `MINIMAL_BUILD` enabled.
- Add dependency-free `korvo1_lcd.c/.h` files.
- Initially use one framebuffer to display black, white, red, green, blue,
  checkerboard, and boundary-marker patterns.
- Verify orientation, colors, the complete visible 800x480 region, and
  continuous VSYNC.

Gate: all 20 cold boots show the correct test pattern, with no unexpected
reset, LCD underrun, or external managed component.

### Phase 2: Testable RGB565 3x Scaler

- Separate the pure scaling function from the ESP-IDF driver.
- Add host tests for a solid color, color bars, corner sentinels, non-default
  pitch, and left/right black bars.
- Generate a stable output hash for fixed input and detect out-of-bounds writes
  on every row.
- Record p50, p95, and maximum scalar execution time on hardware.

Gate: ASan/host tests pass, output is exactly 800x480, and visual inspection
confirms all edges and colors.

### Phase 3: Double Buffering and Frame-Complete Synchronization

- Switch to `num_fbs = 2` and obtain both driver framebuffers.
- Register minimal ISR callbacks and task notification.
- Implement synchronous submission first, then the overlapping front/back
  state machine.
- Add submitted/completed/dropped/timeout/vsync/scale-us metrics.

Gate: a moving vertical-line test runs for at least 10 minutes with no visible
tearing, buffer-reuse race, or timeout.

### Phase 4: Connect the gpSP Video Callback

- Reuse the ESP32-S3 `video_cb()` hash and capture order.
- Accept only 240x160 RGB565; report an invalid size with rate limiting.
- Keep the source frame unmodified, especially by prohibiting an in-place byte
  swap.
- Run a deterministic ROM and compare its serial source-frame hash with the
  existing headless/ESP32-S3 baseline.

Gate: enabling or disabling the LCD does not change gpSP's source-frame hash.
The display is pixel-perfect at 3x with exactly 40 black pixels on each side.

### Phase 5: Stress and Performance Convergence

- Run gpSP, PSRAM framebuffer scanout, and ROM-partition mmap together.
- Record scaling time, wait time, dropped frames, VSYNC count, heap, and minimum
  remaining stack.
- Run continuously for at least 30 minutes.
- Evaluate two fixed 20-line bounce buffers only if measured underruns occur,
  and record the internal-RAM cost.
- If CPU scaling cannot meet its budget, then evaluate ESP-IDF's built-in PPA.
  PPA is not a first-version dependency.
- Once stable, test a 25.5 MHz timing profile (theoretically approximately
  59.711 Hz) to see whether it better matches the GBA frame rate. It may replace
  the official 26 MHz default only if both image quality and stability pass.

Gate: 30 minutes with zero underruns, no permanent deadlock, no sustained heap
decline, and no systematic display-induced frame loss at normal speed.

---

## 10. Build, Dependency Audit, and Hardware Validation

Use `esp32s31/build/` as the standard build directory:

```bash
cd /home/john/work/gpsp/esp32s31
source /home/john/esp-idf/export.sh
idf.py --preview set-target esp32s31
idf.py -B build build
```

Dependency audit:

- The build log must identify the `esp32s31` target.
- `idf.py size-components` must not list LVGL, esp_lvgl_port, the Korvo BSP,
  GT1151, or M5 components.
- Building the display driver must not create `managed_components/` in the
  project tree.
- `REQUIRES` in `CMakeLists.txt` may contain only the ESP-IDF components allowed
  in Section 3 and system components genuinely needed by gpSP.

Hardware test order:

1. Static solid colors and RGB color bars.
2. A one-pixel checkerboard, boundary lines, and a 40/720/40 geometry check.
3. A vertical line moving one pixel per frame to expose tearing.
4. A deterministic gpSP ROM image and framebuffer hash.
5. A 30-minute stress run with serial metrics.

Without physical Korvo-1 hardware, only compilation, host scaler tests, and the
headless degradation path can be completed. Do not report "display driver
validated" merely because the build passes.

---

## 11. Acceptance Criteria

The display milestone is complete only when all of the following are true:

- The ESP-IDF master Preview target builds from a clean build directory.
- There are no external managed graphics or BSP dependencies.
- Korvo-1 V1.1 reliably shows 800x480 RGB565 after a cold boot.
- Red, green, blue, black, and white are correct and the orientation has no
  byte-swap side effects.
- The GBA image is exactly 3x, with a 720x480 active area and 40-pixel black bars
  on both sides.
- The LCD driver never modifies gpSP's source framebuffer.
- Frame-complete events prove double-buffer ownership; no fixed delay is used as
  a substitute.
- The 10-minute tearing test has no tearing, and the 30-minute combined run has
  no underrun, deadlock, or sustained memory decline.
- Serial debugging and the emulator main loop remain usable when the LCD is
  disconnected or initialization fails.
- Metrics distinguish slow scaling, VSYNC wait, LCD timeout, and lack of frames
  from upstream.

---

## 12. Risk Register

| Risk | Mitigation |
|---|---|
| The two official repositories use different timings | Default to the factory-demo 26 MHz profile and retain 18 MHz as a named fallback; converge after recording hardware results |
| Conflicting definitions for GPIO38 | Default to NC and do not drive it automatically; validate only through an explicit experimental profile |
| PSRAM contention among LCD DMA and JIT | Double-buffer, measure scaling/wait time, and add a fixed bounce buffer or lower PCLK only if needed |
| Incorrect RGB565 color order | Confirm with color bars; check data lines and color format before considering any SPI-style byte swap |
| Reusing the front buffer too early | Release it only on frame-complete notification; cover the state machine with timeout metrics |
| Unsafe flash or PSRAM access from an ISR | Keep ISR work to notifications and counters; validate linking and addresses with IRAM-safe configuration |
| Preview API changes | Pin the ESP-IDF commit and rebuild both its RGB example and this driver during upgrades |
| LCD output is not visible in QEMU | Retain the soft-fail/headless path and keep hardware acceptance separate |
| Display work hides an RV32IM issue | Keep test-pattern and scaler tests independent; give the CPU backend its own behavioral gate |
| Copying Apache-2.0 BSP code directly creates maintenance or licensing ambiguity | Extract only hardware facts, write the minimal driver locally, and preserve reference-commit notes |

---

## 13. Suggested Commit Boundaries

1. `docs: plan ESP32-S31 Korvo direct RGB display`
2. `esp32s31: add dependency-free RGB panel smoke test`
3. `esp32s31: add tested RGB565 3x scaler`
4. `esp32s31: add VSYNC-safe double buffering`
5. `esp32s31: wire Korvo display into gpSP video callback`
6. `esp32s31: add display telemetry and hardware soak gate`

Each commit should contain only files for that phase. The existing RV32IM test
and runtime modifications in the repository are separate user work and must not
be modified or committed as part of this display task.
