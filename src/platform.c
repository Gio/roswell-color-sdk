/* platform.c - browser window, picture output, audio device and input
 * devices. This is the only SDK file that uses raylib or Emscripten. */

#ifndef __EMSCRIPTEN__
#error "The Roswell Color SDK targets WebAssembly. Build it with emcc."
#endif

#include <emscripten/emscripten.h>

#include "raylib.h"
#include "internal.h"

#define FRAME_MS        (1000.0 / RC_FRAME_RATE)
#define MAX_CATCH_UP    4
#define LONG_GAP_MS     250.0

/* Audio queue, in stereo sample pairs. */
#define RING_PAIRS      (RC_AUDIO_FRAME_SAMPLES * 8)
#define TARGET_PAIRS    (RC_AUDIO_FRAME_SAMPLES * 3)
#define DRIFT_BAND      (RC_AUDIO_FRAME_SAMPLES * 3 / 2)
#define DRIFT_NUDGE_MS  0.25
#define AUDIO_ALIVE_MS  200.0
#define STICK_THRESHOLD 0.5f
#define GAMEPADS        4

static Texture2D   g_texture;
static AudioStream g_stream;
static bool        g_audio_ok;
static double      g_last_time, g_accumulator, g_last_callback;

static int16_t  g_ring[RING_PAIRS * 2];
static uint32_t g_ring_read, g_ring_count;
static bool     g_buffering = true;
static float    g_hold_l, g_hold_r;

/* Sizes the canvas to the page, tracks its true device-pixel size so whole-number
 * scaling stays sharp on high-DPI displays, stops the game keys from scrolling
 * the page or opening quick find, and starts audio on any user gesture (the
 * audio library alone only listens for clicks and touches). */
EM_JS(void, rc_js_setup, (void), {
    var canvas = Module.canvas;
    var style = document.createElement('style');
    style.textContent =
        'html,body{margin:0;padding:0;width:100%;height:100%;overflow:hidden;background:#000}' +
        'canvas{position:fixed;left:0;top:0;width:100vw;height:100vh;display:block;' +
        'outline:none;image-rendering:pixelated}';
    document.head.appendChild(style);

    var setSize = function(w, h) {
        Module.rcPixelW = Math.max(1, w | 0);
        Module.rcPixelH = Math.max(1, h | 0);
    };
    /* Checked every frame, so a resize never depends on the observer firing. */
    Module.rcMeasure = function() {
        var r = canvas.getBoundingClientRect();
        var d = window.devicePixelRatio || 1;
        var key = r.width + 'x' + r.height + '@' + d;
        if (key === Module.rcSizeKey) return;
        Module.rcSizeKey = key;
        setSize(Math.round(r.width * d), Math.round(r.height * d));
    };
    Module.rcMeasure();

    /* Where supported, this refines the size to the exact device pixels. */
    if (window.ResizeObserver) {
        var observer = new ResizeObserver(function(entries) {
            var box = entries[entries.length - 1].devicePixelContentBoxSize;
            if (box && box[0]) setSize(box[0].inlineSize, box[0].blockSize);
        });
        try { observer.observe(canvas, { box: 'device-pixel-content-box' }); }
        catch (err) { observer.disconnect(); }
    }

    var gameKeys = { ArrowUp: 1, ArrowDown: 1, ArrowLeft: 1, ArrowRight: 1, Space: 1, Slash: 1 };
    window.addEventListener('keydown', function(e) {
        if (gameKeys[e.code]) e.preventDefault();
    }, true);

    Module.rcResumeAudio = function() {
        var m = window.miniaudio;
        if (!m || !m.devices) return;
        for (var i = 0; i < m.devices.length; i++) {
            var d = m.devices[i];
            if (d && d.webaudio && d.webaudio.state !== 'running') d.webaudio.resume();
        }
    };
    ['keydown', 'pointerdown', 'touchend', 'click'].forEach(function(type) {
        document.addEventListener(type, Module.rcResumeAudio, true);
    });
})

EM_JS(void, rc_js_measure, (void), { Module.rcMeasure(); })
EM_JS(int, rc_js_width, (void), { return Module.rcPixelW | 0; })
EM_JS(int, rc_js_height, (void), { return Module.rcPixelH | 0; })
EM_JS(void, rc_js_resume_audio, (void), { if (Module.rcResumeAudio) Module.rcResumeAudio(); })

/* --- Audio --------------------------------------------------------------- */

static void audio_callback(void *buffer, unsigned int frames)
{
    int16_t *out = (int16_t *)buffer;
    unsigned int i;

    g_last_callback = emscripten_get_now();
    if (g_buffering && g_ring_count >= TARGET_PAIRS) g_buffering = false;

    for (i = 0; i < frames; i++) {
        if (!g_buffering && g_ring_count > 0) {
            g_hold_l = g_ring[g_ring_read * 2];
            g_hold_r = g_ring[g_ring_read * 2 + 1];
            g_ring_read = (g_ring_read + 1) % RING_PAIRS;
            g_ring_count--;
        } else {
            /* Ran dry: fade from the last sample instead of clicking. */
            g_buffering = true;
            g_hold_l *= 0.98f;
            g_hold_r *= 0.98f;
        }
        out[i * 2]     = (int16_t)g_hold_l;
        out[i * 2 + 1] = (int16_t)g_hold_r;
    }
}

void rc__platform_audio(const int16_t *samples)
{
    uint32_t i, write;

    if (!g_audio_ok) return;

    if (g_ring_count + RC_AUDIO_FRAME_SAMPLES > RING_PAIRS) {
        uint32_t drop = g_ring_count + RC_AUDIO_FRAME_SAMPLES - RING_PAIRS;
        g_ring_read = (g_ring_read + drop) % RING_PAIRS;
        g_ring_count -= drop;
    }
    write = (g_ring_read + g_ring_count) % RING_PAIRS;
    for (i = 0; i < RC_AUDIO_FRAME_SAMPLES; i++) {
        g_ring[write * 2]     = samples[i * 2];
        g_ring[write * 2 + 1] = samples[i * 2 + 1];
        write = (write + 1) % RING_PAIRS;
    }
    g_ring_count += RC_AUDIO_FRAME_SAMPLES;
}

/* --- Input --------------------------------------------------------------- */

static uint8_t poll_keyboard1(void)
{
    uint8_t keys = 0;

    if (IsKeyDown(KEY_UP))    keys |= RC_BTN_UP;
    if (IsKeyDown(KEY_DOWN))  keys |= RC_BTN_DOWN;
    if (IsKeyDown(KEY_LEFT))  keys |= RC_BTN_LEFT;
    if (IsKeyDown(KEY_RIGHT)) keys |= RC_BTN_RIGHT;
    if (IsKeyDown(KEY_Z))     keys |= RC_BTN_A;
    if (IsKeyDown(KEY_X))     keys |= RC_BTN_B;
    if (IsKeyDown(KEY_C))     keys |= RC_BTN_C;
    if (IsKeyDown(KEY_SPACE)) keys |= RC_BTN_START;
    return keys;
}

static uint8_t poll_keyboard2(void)
{
    uint8_t keys = 0;

    if (IsKeyDown(KEY_W))      keys |= RC_BTN_UP;
    if (IsKeyDown(KEY_S))      keys |= RC_BTN_DOWN;
    if (IsKeyDown(KEY_A))      keys |= RC_BTN_LEFT;
    if (IsKeyDown(KEY_D))      keys |= RC_BTN_RIGHT;
    if (IsKeyDown(KEY_COMMA))  keys |= RC_BTN_A;
    if (IsKeyDown(KEY_PERIOD)) keys |= RC_BTN_B;
    if (IsKeyDown(KEY_SLASH))  keys |= RC_BTN_C;
    if (IsKeyDown(KEY_ENTER))  keys |= RC_BTN_START;
    return keys;
}

uint8_t rc__platform_poll(void)
{
    uint8_t keys = (uint8_t)(poll_keyboard1() | poll_keyboard2()), pad = 0;
    int g;

    for (g = 0; g < GAMEPADS; g++) {
        float sx, sy;
        if (!IsGamepadAvailable(g)) continue;

        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_UP))    pad |= RC_BTN_UP;
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_DOWN))  pad |= RC_BTN_DOWN;
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  pad |= RC_BTN_LEFT;
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) pad |= RC_BTN_RIGHT;
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))  pad |= RC_BTN_A;
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))  pad |= RC_BTN_B;
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) pad |= RC_BTN_C;
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_MIDDLE_RIGHT))     pad |= RC_BTN_START;

        sx = GetGamepadAxisMovement(g, GAMEPAD_AXIS_LEFT_X);
        sy = GetGamepadAxisMovement(g, GAMEPAD_AXIS_LEFT_Y);
        if (sx <= -STICK_THRESHOLD) pad |= RC_BTN_LEFT;
        if (sx >=  STICK_THRESHOLD) pad |= RC_BTN_RIGHT;
        if (sy <= -STICK_THRESHOLD) pad |= RC_BTN_UP;
        if (sy >=  STICK_THRESHOLD) pad |= RC_BTN_DOWN;
    }

    if (pad && g_audio_ok) rc_js_resume_audio();
    return (uint8_t)(keys | pad);
}

/* --- Picture ------------------------------------------------------------- */

static void draw_debug_panel(void)
{
    enum { FONT = 10, LINE = 12, PAD = 4 };
    char text[1024];
    char *start, *p;
    int lines = 1, y;

    if (!rc__debug_panel(text, sizeof text)) return;

    for (p = text; *p; p++) if (*p == '\n' && p[1]) lines++;
    DrawRectangle(0, 0, 232, lines * LINE + PAD * 2, (Color){ 0, 0, 0, 190 });

    y = PAD;
    for (start = p = text; ; p++) {
        if (*p == '\n' || *p == '\0') {
            char end = *p;
            *p = '\0';
            DrawText(start, PAD, y, FONT, GREEN);
            y += LINE;
            if (end == '\0' || p[1] == '\0') break;
            start = p + 1;
        }
    }
}

static void present(bool new_picture)
{
    int w, h, scale;

    rc_js_measure();
    w = rc_js_width();
    h = rc_js_height();
    if (w != GetScreenWidth() || h != GetScreenHeight()) SetWindowSize(w, h);
    if (new_picture) UpdateTexture(g_texture, rc__display_rgba());

    w = GetScreenWidth();
    h = GetScreenHeight();
    scale = w / RC_SCREEN_WIDTH < h / RC_SCREEN_HEIGHT ? w / RC_SCREEN_WIDTH : h / RC_SCREEN_HEIGHT;
    if (scale < 1) scale = 1;

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(g_texture,
                   (Rectangle){ 0, 0, RC_SCREEN_WIDTH, RC_SCREEN_HEIGHT },
                   (Rectangle){ (float)((w - RC_SCREEN_WIDTH * scale) / 2),
                                (float)((h - RC_SCREEN_HEIGHT * scale) / 2),
                                (float)(RC_SCREEN_WIDTH * scale),
                                (float)(RC_SCREEN_HEIGHT * scale) },
                   (Vector2){ 0, 0 }, 0.0f, WHITE);
    draw_debug_panel();
    EndDrawing();
}

/* --- Lifecycle ----------------------------------------------------------- */

static void tick(void *unused)
{
    double now = emscripten_get_now();
    double dt = now - g_last_time;
    int steps = 0;

    (void)unused;
    g_last_time = now;
    if (dt > LONG_GAP_MS) dt = FRAME_MS;    /* returning from a hidden tab */
    g_accumulator += dt;

    /* The display and audio clocks differ slightly. Nudge game time so the audio
     * queue stays near its target instead of slowly running dry or overflowing. */
    if (g_audio_ok && now - g_last_callback < AUDIO_ALIVE_MS) {
        if (g_ring_count > TARGET_PAIRS + DRIFT_BAND) g_accumulator -= DRIFT_NUDGE_MS;
        else if (g_ring_count + DRIFT_BAND < TARGET_PAIRS) g_accumulator += DRIFT_NUDGE_MS;
    }

    while (g_accumulator >= FRAME_MS && steps < MAX_CATCH_UP) {
        rc__run_game_frame();
        g_accumulator -= FRAME_MS;
        steps++;
    }
    if (g_accumulator >= FRAME_MS) g_accumulator = FRAME_MS / 2.0;

    present(steps > 0);
}

void rc__platform_init(const char *title)
{
    Image image = { 0 };

    rc_js_setup();
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(rc_js_width(), rc_js_height(), title);
    SetExitKey(KEY_NULL);

    image.data = (void *)rc__display_rgba();
    image.width = RC_SCREEN_WIDTH;
    image.height = RC_SCREEN_HEIGHT;
    image.mipmaps = 1;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    g_texture = LoadTextureFromImage(image);
    SetTextureFilter(g_texture, TEXTURE_FILTER_POINT);

    InitAudioDevice();
    if (IsAudioDeviceReady()) {
        g_stream = LoadAudioStream(RC_AUDIO_RATE, 16, 2);
        SetAudioStreamCallback(g_stream, audio_callback);
        PlayAudioStream(g_stream);
        g_audio_ok = true;
    }
}

void rc__platform_run(void)
{
    g_last_time = emscripten_get_now();
    g_accumulator = FRAME_MS / 2.0;     /* keeps vsync jitter away from a frame edge */
    emscripten_set_main_loop_arg(tick, NULL, 0, 1);
}
