/*
 * rc_dev.h - Roswell Color SDK, Phase 1 public API (display, audio, controls)
 *
 * Roswell Color is a fantasy console with the display, audio and control limits
 * of the Game Boy Color. It runs at 60 Hz in a browser. It does not emulate the
 * hardware or its registers: games call the functions below.
 *
 * This header is the complete reference. You should not need to read the SDK's
 * source to use it.
 *
 * CONTENTS
 *    1. Quick start
 *    2. Rules that apply everywhere
 *    3. Limits
 *    4. Console lifecycle and the frame
 *    5. Colors and palettes
 *    6. Tiles and tile banks
 *    7. Background and window
 *    8. Sprites
 *    9. Audio
 *   10. Controls
 *   11. Asset data formats
 *   12. Debug
 *
 * ---------------------------------------------------------------------------
 * 1. QUICK START
 * ---------------------------------------------------------------------------
 *
 *     #define RC_DEBUG            // optional: must come before the include
 *     #include "rc_dev.h"
 *
 *     static const uint8_t art[16] = { 0xFF,0x00, 0x81,0x7E, 0x81,0x7E, 0x81,0x7E,
 *                                      0x81,0x7E, 0x81,0x7E, 0x81,0x7E, 0xFF,0x00 };
 *     static int px = 76;
 *
 *     static void frame(void *user) {
 *         (void)user;
 *         if (rc_held(RC_BTN_LEFT))  px--;
 *         if (rc_held(RC_BTN_RIGHT)) px++;
 *         rc_sprite_move(0, px, 68);
 *     }
 *
 *     int main(void) {
 *         static const int bank_sizes[1] = { 16 };
 *         static const rc_color colors[3] = {
 *             RC_RGB(31,31,31), RC_RGB(31,0,0), RC_RGB(0,0,0) };
 *
 *         rc_init("My game");
 *         rc_banks_setup(bank_sizes, 1);
 *         rc_bank_load(0, 0, art, 1);
 *         rc_sprite_bind(0, 0, 0);
 *         rc_sprite_palette(0, colors);
 *         rc_sprite_set(0, px, 68, 0, RC_SPRITE_PALETTE(0));
 *         rc_run(frame, NULL);    // never returns in the browser
 *         return 0;
 *     }
 *
 * To build a program, run this from WSL at the repository root:
 *
 *     make GAME=path/to/main.c
 *
 * That writes tmp/web/main.html, main.js and main.wasm. Open the page through
 * a local web server. A program that uses rc_init_headless() can also run with
 * `node tmp/web/main.js`. Programs are compiled as strict C99 (-std=c99), where
 * Emscripten's EM_ASM macro is unavailable. Use EM_JS instead if you need it.
 *
 * ---------------------------------------------------------------------------
 * 2. RULES THAT APPLY EVERYWHERE
 * ---------------------------------------------------------------------------
 *
 * Retained mode. A game sets up tiles, palettes, maps and sprites, then changes
 * only what moves. Everything stays in place until changed. There is no
 * per-frame draw call.
 *
 * No error codes. Like the hardware, the SDK never fails a call:
 *   - An out-of-range INDEX (a sprite, palette, bank, channel, map cell...)
 *     makes the call do nothing.
 *   - An out-of-range VALUE (a volume, period, length...) is clamped to the
 *     nearest legal value.
 *   - A block that only partly fits (tiles into a bank, a map onto a layer)
 *     copies the part that fits.
 * With RC_DEBUG defined, each of these also prints a warning naming your source
 * file and line. See section 12.
 *
 * Coordinates. The screen origin (0,0) is the top-left pixel. X grows right and
 * Y grows down.
 *
 * Threads. The SDK is single-threaded. Call it only from the main thread.
 */

#ifndef RC_DEV_H
#define RC_DEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * 3. LIMITS
 * ------------------------------------------------------------------------- */

/* Display */
#define RC_SCREEN_WIDTH            160  /* pixels */
#define RC_SCREEN_HEIGHT           144  /* pixels, also the number of scanlines */
#define RC_FRAME_RATE               60  /* frames per second, exactly */

/* Color */
#define RC_CHANNEL_MAX              31  /* 5 bits (32 levels) per R, G, B */
#define RC_TOTAL_COLORS          32768  /* 15-bit RGB */
#define RC_BG_PALETTES               8
#define RC_BG_PALETTE_COLORS         4
#define RC_SPRITE_PALETTES           8
#define RC_SPRITE_PALETTE_COLORS     3  /* plus color 0, always transparent */
#define RC_MAX_SCREEN_COLORS        56  /* 8*4 background + 8*3 sprite */

/* Tiles */
#define RC_TILE_PIXELS               8  /* tiles are 8x8 */
#define RC_TILE_BYTES               16  /* 2 bits per pixel */
#define RC_MAX_TILES              1024  /* tiles held by all banks together */
#define RC_MAX_BANKS              1024  /* a bank holds at least one tile */
#define RC_BIND_SLOTS                2  /* banks selectable per layer group */
#define RC_SLOT_TILES              256  /* tile indexes per slot */
#define RC_MAX_BG_TILES            512  /* background + window, at once */
#define RC_MAX_SPRITE_TILES        512  /* sprites, at once */

/* Background and window */
#define RC_MAP_SIZE                 32  /* maps are 32x32 tiles (256x256 px) */

/* Sprites */
#define RC_MAX_SPRITES              40  /* entries in the sprite table */
#define RC_MAX_SPRITES_PER_LINE     10

/* Audio */
#define RC_AUDIO_CHANNELS            4
#define RC_AUDIO_RATE            44100  /* Hz, fixed */
#define RC_AUDIO_FRAME_SAMPLES     735  /* stereo samples per frame (44100/60) */
#define RC_PERIOD_MAX             2047  /* 11 bits */
#define RC_VOLUME_MAX               15  /* 4 bits (16 levels) */
#define RC_ENV_PACE_MAX              7
#define RC_PULSE_LENGTH_MAX         64  /* also used by noise */
#define RC_NOISE_LENGTH_MAX         64
#define RC_WAVE_LENGTH_MAX         256
#define RC_SWEEP_PACE_MAX            7
#define RC_SWEEP_SHIFT_MAX           7
#define RC_NOISE_SHIFT_MAX          15
#define RC_NOISE_DIVISOR_MAX         7
#define RC_WAVE_SAMPLES             32  /* 4 bits each */
#define RC_WAVE_BYTES               16
#define RC_MASTER_MAX                7  /* 8 levels, level 0 is still audible */

/* Controls */
#define RC_BUTTON_COUNT              8


/* ---------------------------------------------------------------------------
 * 4. CONSOLE LIFECYCLE AND THE FRAME
 * ---------------------------------------------------------------------------
 *
 * One frame, in rc_run():
 *   1. The 8 inputs are polled, once.
 *   2. Your frame function runs.
 *   3. The picture is composed from the retained state.
 *   4. Music advances one frame and 735 stereo audio samples are mixed.
 *   5. The picture is shown.
 *
 * rc_run() runs game frames at exactly 60 per second on any monitor refresh
 * rate. If the browser falls behind it runs at most 4 frames to catch up, then
 * drops the remaining time. While the tab is hidden the browser stops calling
 * it, so the game and its audio pause.
 *
 * rc_step() does steps 1, 3 and 4 immediately and returns, so a test can drive
 * the console without real time passing. Your code runs between calls:
 *
 *     rc_input_feed(RC_BTN_A);
 *     rc_step();
 *     // rc_pressed(RC_BTN_A) is now true
 *
 * Display: the 160x144 picture is scaled by the largest whole number that fits
 * the window (never a fraction), centered, with black borders. The scale is
 * recalculated when the window is resized. If the window is smaller than
 * 160x144 the scale stays 1 and the picture is cropped.
 * ------------------------------------------------------------------------- */

/* A function called once per frame by rc_run(). */
typedef void (*rc_frame_fn)(void *user);

/* Opens the display and audio output, then resets the console (see rc_reset).
 * Call once, before anything else. Calling it again only resets the console.
 * title: browser tab title. NULL means "Roswell Color". */
void rc_init(const char *title);

/* Starts the console with no window, audio device or real input devices, for
 * automated tests. It works in the browser and under Node.js. rc_step() and
 * every query work normally. rc_run() returns immediately. Resets the console.
 * May be called repeatedly to start each test from a clean console.
 * The mode is fixed by whichever init runs first: after this call rc_init() only
 * resets, and if a window is already open this call only resets. */
void rc_init_headless(void);

/* Returns every setting to its power-on state:
 *   no banks or tiles; all slots unbound; all palettes black; both maps filled
 *   with zero entries; scroll (0,0); window hidden at (0,0); all sprites hidden;
 *   sprite size 8x8; all channels stopped and panned to both sides; master
 *   volume 7 on both sides; Wave RAM zeroed; no music; no inputs held; input
 *   feed off; frame counter 0; debug counters cleared. */
void rc_reset(void);

/* Runs the game loop, calling frame(user) 60 times per second.
 * In the browser this never returns. frame must not be NULL.
 * With rc_init_headless() it returns immediately without calling frame. */
void rc_run(rc_frame_fn frame, void *user);

/* Advances one frame immediately: polls input, composes the picture and mixes
 * 735 audio samples. Ignored when called from inside a frame function. It does
 * not update the visible window, which only rc_run() does. Read the result with
 * rc_debug_pixel() and rc_debug_audio(). */
void rc_step(void);

/* Number of frames completed since the last reset. */
uint32_t rc_frame_count(void);


/* ---------------------------------------------------------------------------
 * 5. COLORS AND PALETTES
 * ---------------------------------------------------------------------------
 *
 * A color is 15-bit RGB, stored as 0bbbbbgggggrrrrr. Build one with RC_RGB().
 * Bit 15 is ignored. On screen each 5-bit channel v becomes the 8-bit value
 * (v << 3) | (v >> 2), with no LCD color correction.
 * ------------------------------------------------------------------------- */

typedef uint16_t rc_color;

/* Builds a color from channels 0..31. Higher bits of each channel are dropped. */
#define RC_RGB(r, g, b) \
    ((rc_color)(((r) & 31) | (((g) & 31) << 5) | (((b) & 31) << 10)))

#define RC_COLOR_R(c) ((c) & 31)
#define RC_COLOR_G(c) (((c) >> 5) & 31)
#define RC_COLOR_B(c) (((c) >> 10) & 31)

/* Sets background palette 0..7 from 4 colors. Pixel values 0..3 use colors[0..3]. */
void rc_bg_palette(int palette, const rc_color colors[RC_BG_PALETTE_COLORS]);

/* Sets sprite palette 0..7 from 3 colors. Sprite pixel values 1..3 use
 * colors[0..2]. Pixel value 0 is always transparent and has no color. */
void rc_sprite_palette(int palette, const rc_color colors[RC_SPRITE_PALETTE_COLORS]);


/* ---------------------------------------------------------------------------
 * 6. TILES AND TILE BANKS
 * ---------------------------------------------------------------------------
 *
 * A tile is 8x8 pixels with 4 possible values, stored in 16 bytes (see section
 * 11). Tiles live in BANKS. You choose how many banks there are and how big
 * each one is, up to RC_MAX_TILES (1,024) tiles in total.
 *
 * Banks are not drawn directly. The background/window group and the sprite
 * group each have 2 SLOTS. Binding a slot points it at up to 256 consecutive
 * tiles of a bank. A map entry or sprite then picks slot 0 or 1 plus a tile
 * index 0..255. So each group can reach at most 2 * 256 = 512 tiles at once,
 * and it cannot exceed that limit. Both groups may bind the same bank.
 *
 * A tile index that lands past the end of its bank, or in an unbound slot,
 * draws as pixel value 0 (background color 0, or transparent for a sprite).
 *
 * Rebinding a slot takes effect on the next composed frame and is how a game
 * swaps art. It copies nothing.
 * ------------------------------------------------------------------------- */

/* Passing this as a bank to rc_bg_bind/rc_sprite_bind unbinds the slot. */
#define RC_NO_BANK (-1)

/* Replaces the whole bank layout. Bank i gets sizes[i] tiles. All tile data is
 * cleared to zero and all slots are unbound.
 * - More than RC_MAX_BANKS banks: the extra banks are not created.
 * - A negative size counts as 0.
 * - When the 1,024-tile pool runs out, that bank is cut to the tiles left and
 *   later banks get size 0.
 * Returns the number of banks created. Use rc_bank_size() to read actual sizes. */
int rc_banks_setup(const int *sizes, int count);

/* Number of banks in the current layout. */
int rc_bank_count(void);

/* Actual size of a bank in tiles, or 0 if it doesn't exist. */
int rc_bank_size(int bank);

/* Copies count tiles (16 bytes each) into a bank, starting at tile first.
 * The data is copied, so it need not stay valid. Tiles past the end of the
 * bank are not copied. */
void rc_bank_load(int bank, int first, const uint8_t *tiles, int count);

/* Zeroes all tiles of a bank and marks them as not loaded. */
void rc_bank_clear(int bank);

/* Points background/window slot 0..1 at tiles first..first+255 of a bank. */
void rc_bg_bind(int slot, int bank, int first);

/* Points sprite slot 0..1 at tiles first..first+255 of a bank. */
void rc_sprite_bind(int slot, int bank, int first);


/* ---------------------------------------------------------------------------
 * 7. BACKGROUND AND WINDOW
 * ---------------------------------------------------------------------------
 *
 * There are two layers, each a 32x32 map of tiles:
 *   RC_LAYER_BG      scrolls and wraps around at 256 pixels.
 *   RC_LAYER_WINDOW  does not scroll. It is drawn opaque over the background,
 *                    from its top-left position to the screen's bottom-right.
 *
 * Map entry attributes (combine with |):
 *   RC_MAP_PALETTE(n)  background palette 0..7
 *   RC_MAP_SLOT1       use slot 1 (default slot 0)
 *   RC_FLIP_X          mirror horizontally
 *   RC_FLIP_Y          mirror vertically
 *   RC_MAP_PRIORITY    this tile's pixel values 1..3 draw over all sprites
 * ------------------------------------------------------------------------- */

enum { RC_LAYER_BG = 0, RC_LAYER_WINDOW = 1 };

#define RC_MAP_PALETTE(n)  ((uint8_t)((n) & 7))
#define RC_MAP_SLOT1       0x08
#define RC_FLIP_X          0x20
#define RC_FLIP_Y          0x40
#define RC_MAP_PRIORITY    0x80

/* One map cell. */
typedef struct rc_map_entry {
    uint8_t tile;   /* tile index 0..255 within the chosen slot */
    uint8_t attr;   /* attribute bits above */
} rc_map_entry;

/* A rectangular map stored as width*height entries, row by row. See section 11. */
typedef struct rc_map_data {
    const rc_map_entry *entries;
    uint8_t width;   /* 1..32 */
    uint8_t height;  /* 1..32 */
} rc_map_data;

/* Sets one cell. x and y are 0..31. */
void rc_map_set(int layer, int x, int y, rc_map_entry entry);

/* Reads one cell. Out-of-range arguments return a zero entry. */
rc_map_entry rc_map_get(int layer, int x, int y);

/* Sets every cell of a layer. */
void rc_map_fill(int layer, rc_map_entry entry);

/* Copies a rectangular map onto a layer with its top-left at (x, y). Cells
 * outside the 32x32 map are not copied. */
void rc_map_load(int layer, int x, int y, const rc_map_data *map);

/* Sets the background scroll: the map pixel shown at the screen's top-left.
 * Values wrap around at 256, so any integer is valid. */
void rc_bg_scroll(int x, int y);

/* Places the window's top-left corner on screen. x below -7 counts as -7, the
 * furthest left the hardware allows. A negative y shifts the window's content
 * up. A window with x >= 160 or y >= 144 is entirely off screen. */
void rc_window_position(int x, int y);

/* Shows or hides the window. It starts hidden. */
void rc_window_show(bool show);


/* ---------------------------------------------------------------------------
 * 8. SPRITES
 * ---------------------------------------------------------------------------
 *
 * The sprite table has 40 entries, indexes 0..39. A sprite's (x, y) is the
 * screen position of its top-left pixel and may be off screen.
 *
 * Going over a limit behaves like the hardware:
 *   - For each scanline the table is checked in order, and only the first 10
 *     visible sprites that cover that line are drawn. The rest are dropped on
 *     that line only.
 *   - Only the vertical position is checked, so a sprite that is off screen
 *     horizontally still uses one of the line's 10 places.
 *   - Hidden sprites never count.
 *   - Where sprites overlap, the one earlier in the table is on top.
 *
 * Priority against the background, per pixel:
 *   1. Find the earliest-in-table drawn sprite with a non-transparent pixel.
 *      Later sprites are ignored at this pixel, even if this one ends up behind
 *      the background.
 *   2. If the background/window pixel value is 0, the sprite is drawn.
 *   3. Otherwise, if the map entry has RC_MAP_PRIORITY or the sprite has
 *      RC_SPRITE_BEHIND, the background is drawn.
 *   4. Otherwise the sprite is drawn.
 *
 * Sprite attributes (combine with |):
 *   RC_SPRITE_PALETTE(n)  sprite palette 0..7
 *   RC_SPRITE_SLOT1       use slot 1 (default slot 0)
 *   RC_FLIP_X, RC_FLIP_Y  mirror the sprite
 *   RC_SPRITE_BEHIND      background pixel values 1..3 draw over this sprite
 *
 * In 8x16 mode a sprite uses two consecutive tiles. Bit 0 of its tile index is
 * ignored: the top half uses (tile & 0xFE) and the bottom half (tile | 0x01).
 * RC_FLIP_Y flips the whole 8x16 sprite, swapping the halves.
 * ------------------------------------------------------------------------- */

#define RC_SPRITE_PALETTE(n)  ((uint8_t)((n) & 7))
#define RC_SPRITE_SLOT1       0x08
#define RC_SPRITE_BEHIND      0x80

enum { RC_SPRITE_8X8 = 0, RC_SPRITE_8X16 = 1 };

/* Sets the size of ALL sprites: RC_SPRITE_8X8 or RC_SPRITE_8X16. */
void rc_sprite_size(int size);

/* Sets every field of a sprite and makes it visible. x and y are clamped to
 * -32768..32767. */
void rc_sprite_set(int sprite, int x, int y, uint8_t tile, uint8_t attr);

/* Changes one part of a sprite. These don't change whether it is visible. */
void rc_sprite_move(int sprite, int x, int y);
void rc_sprite_tile(int sprite, uint8_t tile);
void rc_sprite_attr(int sprite, uint8_t attr);

/* Makes a sprite visible or hidden, keeping its other fields. */
void rc_sprite_show(int sprite, bool show);

/* Hides all 40 sprites. */
void rc_sprites_hide_all(void);


/* ---------------------------------------------------------------------------
 * 9. AUDIO
 * ---------------------------------------------------------------------------
 *
 *   RC_CH1  Pulse with frequency sweep
 *   RC_CH2  Pulse
 *   RC_CH3  Wave (32 samples of 4 bits)
 *   RC_CH4  Noise (15-bit or 7-bit LFSR)
 *
 * Start a sound by filling an rc_voice and calling rc_audio_play(). Unset fields
 * of a C99 designated initializer are zero. Each channel reads only its fields:
 *
 *   field         CH1 CH2 CH3 CH4  range and meaning
 *   period         x   x   x       0..2047, higher is higher pitch
 *   length         x   x   x   x   0 = play until stopped.
 *                                  1..64 (CH1,2,4) or 1..256 (CH3): plays for
 *                                  length/256 seconds, then stops
 *   volume         x   x       x   0..15, starting volume
 *   env_dir        x   x       x   RC_ENV_DOWN or RC_ENV_UP
 *   env_pace       x   x       x   0 = no envelope. 1..7: volume changes by 1
 *                                  every pace/64 s, stopping at 0 or 15
 *   duty           x   x           RC_DUTY_12, _25, _50, _75
 *   sweep_pace     x               0 = no sweep. 1..7: a step every pace/128 s
 *   sweep_dir      x               RC_SWEEP_UP or RC_SWEEP_DOWN
 *   sweep_shift    x               0..7. Each step changes the period by
 *                                  period >> shift. 0 = no sweep
 *   wave_level             x       RC_WAVE_MUTE, _100, _50, _25
 *   noise_width                x   RC_NOISE_15BIT or RC_NOISE_7BIT
 *   noise_shift                x   0..15. 14 and 15 freeze the LFSR
 *   noise_divisor              x   0..7
 *
 * Pitch (the same as the hardware, in real Hz):
 *   CH1, CH2:  131072 / (2048 - period) Hz
 *   CH3:        65536 / (2048 - period) Hz
 *   CH4 LFSR clock: 262144 / (d * 2^noise_shift) Hz, where d = noise_divisor,
 *   or d = 0.5 when noise_divisor is 0.
 *
 * Timing. Sound time is counted in 44.1 kHz samples, so durations are exact in
 * seconds and don't depend on the frame rate.
 *
 * A pulse or noise voice with volume 0 and RC_ENV_DOWN can never make sound, so
 * like the hardware it doesn't start, and any sound on that channel stops.
 *
 * A sweep step up that would pass period 2047 stops the channel. The check also
 * runs when the note starts and after each step.
 *
 * Mixing: each channel contributes equally. The panned sum is scaled by
 * (master + 1) / 8, so master level 0 is 1/8 volume, never silent. A DC-blocking
 * filter then removes the constant offset, as the hardware's output capacitor
 * does. Output is 16-bit stereo at 44.1 kHz; other device rates are resampled by
 * the browser.
 *
 * Browsers keep audio silent until the player interacts with the page. The SDK
 * starts it on the first key press, click, touch or gamepad button.
 * ------------------------------------------------------------------------- */

enum { RC_CH1 = 0, RC_CH2 = 1, RC_CH3 = 2, RC_CH4 = 3 };
enum { RC_DUTY_12 = 0, RC_DUTY_25 = 1, RC_DUTY_50 = 2, RC_DUTY_75 = 3 };
enum { RC_ENV_DOWN = 0, RC_ENV_UP = 1 };
enum { RC_SWEEP_UP = 0, RC_SWEEP_DOWN = 1 };
enum { RC_WAVE_MUTE = 0, RC_WAVE_100 = 1, RC_WAVE_50 = 2, RC_WAVE_25 = 3 };
enum { RC_NOISE_15BIT = 0, RC_NOISE_7BIT = 1 };
enum { RC_PAN_OFF = 0, RC_PAN_LEFT = 1, RC_PAN_RIGHT = 2, RC_PAN_BOTH = 3 };

/* A sound to play on a channel. See the table above. */
typedef struct rc_voice {
    uint16_t period;
    uint16_t length;
    uint8_t  volume;
    uint8_t  env_dir;
    uint8_t  env_pace;
    uint8_t  duty;
    uint8_t  sweep_pace;
    uint8_t  sweep_dir;
    uint8_t  sweep_shift;
    uint8_t  wave_level;
    uint8_t  noise_width;
    uint8_t  noise_shift;
    uint8_t  noise_divisor;
} rc_voice;

/* Starts (or restarts from its beginning) a sound on a channel. */
void rc_audio_play(int channel, const rc_voice *voice);

/* Stops a channel immediately. */
void rc_audio_stop(int channel);

/* Changes the pitch of CH1, CH2 or CH3 without restarting the note. On CH1 this
 * also becomes the sweep's current period. Ignored for CH4. */
void rc_audio_period(int channel, int period);

/* Sends a channel to RC_PAN_OFF, _LEFT, _RIGHT or _BOTH. Default is both. */
void rc_audio_pan(int channel, int pan);

/* Sets master volume per side, 0..7. Level 0 is 1/8 volume. Default is 7. */
void rc_audio_master(int left, int right);

/* Loads the 16 bytes of Wave RAM (32 samples). The high nibble of each byte is
 * played first. Takes effect immediately, even during a note. */
void rc_audio_wave(const uint8_t wave[RC_WAVE_BYTES]);

/* True while the channel is producing sound. */
bool rc_audio_active(int channel);

/* --- Music (data format in section 11) ----------------------------------- */

typedef struct rc_music rc_music;

/* Starts a song from its first frame, replacing any current song. The data must
 * stay valid while it plays. Music and sound effects share the 4 channels: a
 * later music event on a channel replaces whatever is playing there. */
void rc_music_play(const rc_music *music);

/* Stops the song and silences the channels it has used. */
void rc_music_stop(void);

/* True while a song is playing. */
bool rc_music_playing(void);


/* ---------------------------------------------------------------------------
 * 10. CONTROLS
 * ---------------------------------------------------------------------------
 *
 *   Input                  keyboard1    keyboard2  Gamepad (by position)
 *   Up, Down, Left, Right  Arrow keys   W S A D    D-pad or left stick
 *   A                      Z            ,          Left face (Square / Xbox X)
 *   B                      X            .          Bottom face (Cross / Xbox A)
 *   C                      C            /          Right face (Circle / Xbox B)
 *   Start                  Space        Enter      Right center (Options / Menu)
 *
 * Mappings are fixed. Both keyboard sets and all connected gamepads work
 * together. A stick direction counts as pressed past half of its travel.
 *
 * Input is polled once per frame, so these values don't change during a frame.
 * Each function takes one RC_BTN_ value, or several joined with |, and returns
 * true if ANY of them matches.
 *
 * Opposite directions (Left/Right, Up/Down) are never active together:
 *   - The one pressed most recently is active and the other reads as not held.
 *   - If that one is released while the first is still down, the first becomes
 *     active again. It reads as held, NOT as just pressed.
 *   - A direction that loses to its opposite does NOT read as just released,
 *     because the player hasn't let go. It reads as just released on the frame
 *     the player really lets go.
 *   - If both are pressed on the very same frame, Right beats Left and Down
 *     beats Up.
 * ------------------------------------------------------------------------- */

#define RC_BTN_UP      0x01
#define RC_BTN_DOWN    0x02
#define RC_BTN_LEFT    0x04
#define RC_BTN_RIGHT   0x08
#define RC_BTN_A       0x10
#define RC_BTN_B       0x20
#define RC_BTN_START   0x40
#define RC_BTN_C       0x80
#define RC_BTN_ALL     0xFF

/* True while the input is active. */
bool rc_held(int buttons);

/* True only on the frame the input was pressed. */
bool rc_pressed(int buttons);

/* True only on the frame the player let go of the input. */
bool rc_released(int buttons);

/* Feeds input from code. From the next poll on, exactly these inputs count as
 * physically down and real devices are ignored. The state stays until changed,
 * so call it only when the inputs change. Opposite directions follow the same
 * rules as a real player. */
void rc_input_feed(int buttons);

/* Stops feeding input and returns to real devices. */
void rc_input_feed_stop(void);


/* ---------------------------------------------------------------------------
 * 11. ASSET DATA FORMATS
 * ---------------------------------------------------------------------------
 *
 * Art and music are converted ahead of time into C data and compiled in. The
 * conversion tools come in a later phase. These are the formats they target.
 *
 * TILES: a const uint8_t array of 16 bytes per tile. Each of the 8 rows is two
 * bytes: first the low bits, then the high bits of its 8 pixels. Bit 7 is the
 * leftmost pixel. Pixel value = (high bit << 1) | low bit.
 *
 *     // Row of pixels 0,1,2,3,0,1,2,3 -> low 0x55, high 0x33
 *     static const uint8_t tiles[] = { 0x55,0x33, ... };   // 16 bytes per tile
 *
 * PALETTES: const rc_color arrays. See rc_palette_data below.
 *
 * MAPS: a const rc_map_entry array of width*height entries, row by row, wrapped
 * in an rc_map_data (section 7).
 *
 * WAVES: a const uint8_t array of 16 bytes, high nibble played first.
 *
 * MUSIC: a table of timed events, sorted by frame, wrapped in an rc_music.
 *
 *     static const uint8_t lead_wave[16] = { 0x01,0x23, ... };
 *     static const rc_music_event events[] = {
 *         { .frame = 0,  .channel = RC_CH3, .op = RC_MUSIC_WAVE, .wave = lead_wave },
 *         { .frame = 0,  .channel = RC_CH1, .op = RC_MUSIC_PLAY,
 *           .voice = { .period = 1750, .volume = 12, .duty = RC_DUTY_50 } },
 *         { .frame = 30, .channel = RC_CH1, .op = RC_MUSIC_STOP },
 *     };
 *     static const rc_music song = { events, 3, 60, 0 };   // loops to frame 0
 * ------------------------------------------------------------------------- */

/* A set of palettes. bg points to bg_count*4 colors and sprite points to
 * sprite_count*3 colors. Counts are 0..8. */
typedef struct rc_palette_data {
    const rc_color *bg;
    const rc_color *sprite;
    uint8_t bg_count;
    uint8_t sprite_count;
} rc_palette_data;

/* Loads palettes 0..count-1 of each kind. */
void rc_palette_load(const rc_palette_data *data);

/* What a music event does. */
enum {
    RC_MUSIC_PLAY = 0,   /* rc_audio_play(channel, &voice) */
    RC_MUSIC_STOP,       /* rc_audio_stop(channel) */
    RC_MUSIC_PERIOD,     /* rc_audio_period(channel, value) */
    RC_MUSIC_PAN,        /* rc_audio_pan(channel, value) */
    RC_MUSIC_WAVE        /* rc_audio_wave(wave); channel is ignored */
};

typedef struct rc_music_event {
    uint32_t frame;        /* frames since song start; must not decrease */
    uint8_t  channel;      /* RC_CH1..RC_CH4 */
    uint8_t  op;           /* RC_MUSIC_... */
    uint16_t value;        /* for RC_MUSIC_PERIOD and RC_MUSIC_PAN */
    const uint8_t *wave;   /* for RC_MUSIC_WAVE: 16 bytes */
    rc_voice voice;        /* for RC_MUSIC_PLAY */
} rc_music_event;

/* RC_MUSIC_NO_LOOP as loop_frame makes a song end instead of looping. */
#define RC_MUSIC_NO_LOOP (-1)

struct rc_music {
    const rc_music_event *events;
    uint32_t event_count;
    uint32_t frame_count;   /* song length in frames (60 = one second) */
    int32_t  loop_frame;    /* frame to return to at the end, or RC_MUSIC_NO_LOOP */
};


/* ---------------------------------------------------------------------------
 * 12. DEBUG
 * ---------------------------------------------------------------------------
 *
 * Turn debug on with one line BEFORE including this header:
 *
 *     #define RC_DEBUG
 *     #include "rc_dev.h"
 *
 * It is checked with #ifdef, so any definition counts, even `#define RC_DEBUG 0`.
 *
 * With RC_DEBUG:
 *   - Every SDK call with a limit is replaced by a checked version that prints a
 *     warning with your file and line when it goes over that limit, then does
 *     exactly what the normal function does. Each different warning prints once
 *     per source line, and rc_usage.warnings counts every occurrence.
 *   - The console log reports the sprites dropped, whenever that set changes.
 *   - An on-screen panel shows usage against each limit, each audio channel, and
 *     the 8 inputs. It draws over the window, never into the 160x144 picture, so
 *     rc_debug_pixel() is unaffected. Turn it off with rc_debug_overlay(false).
 *   - The rc_debug_* functions below return real data.
 *
 * Without RC_DEBUG, the rc_debug_* functions still compile but do nothing and
 * report zeros, so debug calls can be left in a release build.
 *
 * Per-frame values describe the most recently completed frame.
 * ------------------------------------------------------------------------- */

/* Usage against each limit. */
typedef struct rc_usage {
    int banks;                  /* banks in the layout, of RC_MAX_BANKS */
    int tiles_reserved;         /* tiles in all banks, of RC_MAX_TILES */
    int tiles_loaded;           /* tiles written and not cleared, of RC_MAX_TILES */
    int bg_tiles_bound;         /* distinct tiles reachable, of RC_MAX_BG_TILES */
    int sprite_tiles_bound;     /* distinct tiles reachable, of RC_MAX_SPRITE_TILES */
    int sprites_visible;        /* visible table entries, of RC_MAX_SPRITES */
    int busiest_line;           /* scanline with the most sprites covering it, or -1 */
    int busiest_line_sprites;   /* how many cover it (may exceed 10) */
    int sprites_dropped;        /* sprites dropped on at least one line */
    int palette_colors_shown;   /* palette entries on screen, of RC_MAX_SCREEN_COLORS */
    uint32_t warnings;          /* limit warnings since the last reset */
} rc_usage;

/* Which sprites cover one scanline. */
typedef struct rc_line_info {
    int covering;                               /* visible sprites covering the line */
    int drawn_count;                            /* 0..10 */
    int dropped_count;                          /* covering - drawn_count */
    uint8_t drawn[RC_MAX_SPRITES_PER_LINE];     /* sprite indexes, in table order */
    uint8_t dropped[RC_MAX_SPRITES];            /* sprite indexes, in table order */
} rc_line_info;

/* What an audio channel is playing right now. */
typedef struct rc_channel_info {
    bool  active;
    int   period;        /* current period, after any sweep */
    float frequency_hz;  /* tone frequency, or LFSR clock on CH4 */
    int   volume;        /* current envelope volume (CH1, CH2, CH4) */
    int   duty;          /* CH1, CH2 */
    int   env_dir;
    int   env_pace;
    int   length_left;   /* 1/256 s ticks left, 0 = no length limit */
    int   pan;           /* RC_PAN_... */
    int   sweep_pace;    /* CH1 */
    int   sweep_dir;     /* CH1 */
    int   sweep_shift;   /* CH1 */
    int   wave_level;    /* CH3 */
    int   noise_width;   /* CH4 */
    int   noise_shift;   /* CH4 */
    int   noise_divisor; /* CH4 */
} rc_channel_info;

/* The 8 inputs this frame, as RC_BTN_ bitmasks. */
typedef struct rc_input_info {
    uint8_t held;       /* active inputs, after opposite-direction rules */
    uint8_t pressed;
    uint8_t released;
    uint8_t physical;   /* physically down, before opposite-direction rules */
} rc_input_info;

/* Library side of the debug API. Use the rc_debug_* functions instead. */
void rc__dbg_enable(void);
void rc__dbg_warn(const char *file, int line, const char *fmt, ...);
void rc__dbg_usage(rc_usage *out);
void rc__dbg_line(int line, rc_line_info *out);
int  rc__dbg_dropped(int out[RC_MAX_SPRITES]);
int  rc__dbg_sprite_dropped_lines(int sprite);
void rc__dbg_channel(int channel, rc_channel_info *out);
void rc__dbg_input(rc_input_info *out);
rc_color rc__dbg_pixel(int x, int y);
const int16_t *rc__dbg_audio(void);
void rc__dbg_overlay(bool show);
void rc__dbg_print(void);

#ifdef RC_DEBUG

/* Fills out with usage against each limit. */
static inline void rc_debug_usage(rc_usage *out) { rc__dbg_usage(out); }

/* Fills out with the sprites drawn and dropped on scanline 0..143. */
static inline void rc_debug_line(int line, rc_line_info *out) { rc__dbg_line(line, out); }

/* Writes the indexes of sprites dropped on at least one line, ascending, and
 * returns how many. out must hold RC_MAX_SPRITES ints. */
static inline int rc_debug_dropped(int out[RC_MAX_SPRITES]) { return rc__dbg_dropped(out); }

/* Number of scanlines on which this sprite was dropped. */
static inline int rc_debug_sprite_dropped_lines(int sprite) { return rc__dbg_sprite_dropped_lines(sprite); }

/* Fills out with what a channel is playing. */
static inline void rc_debug_channel(int channel, rc_channel_info *out) { rc__dbg_channel(channel, out); }

/* Fills out with this frame's input state. */
static inline void rc_debug_input(rc_input_info *out) { rc__dbg_input(out); }

/* Returns the 15-bit color of a composed pixel, or 0 if off screen. */
static inline rc_color rc_debug_pixel(int x, int y) { return rc__dbg_pixel(x, y); }

/* Returns the last frame's audio: RC_AUDIO_FRAME_SAMPLES stereo pairs (1,470
 * int16_t values, left first). Valid until the next frame. */
static inline const int16_t *rc_debug_audio(void) { return rc__dbg_audio(); }

/* Shows or hides the on-screen debug panel. It starts shown. */
static inline void rc_debug_overlay(bool show) { rc__dbg_overlay(show); }

/* Prints usage, dropped sprites, channels and inputs to the console. */
static inline void rc_debug_print(void) { rc__dbg_print(); }

/* --- Checked versions of the SDK functions -------------------------------- */

#define RC__WARN_IF(cond, ...) \
    do { if (cond) rc__dbg_warn(file, line, __VA_ARGS__); } while (0)
#define RC__RANGE(name, v, lo, hi) \
    RC__WARN_IF((v) < (lo) || (v) > (hi), "%s %d is outside %d..%d", name, (int)(v), (int)(lo), (int)(hi))
#define RC__BANK(b) \
    RC__WARN_IF((b) < 0 || (b) >= (rc_bank_count)(), "bank %d doesn't exist (%d banks set up)", (b), (rc_bank_count)())

static inline void rc__d_colors(const char *file, int line, const rc_color *c, int n) {
    int i;
    RC__WARN_IF(!c, "palette colors are NULL");
    if (c) for (i = 0; i < n; i++) RC__WARN_IF(c[i] > 0x7FFF, "color 0x%04X uses bit 15, which is ignored", c[i]);
}
static inline void rc__d_bg_palette(const char *file, int line, int p, const rc_color *c) {
    RC__RANGE("background palette", p, 0, RC_BG_PALETTES - 1);
    rc__d_colors(file, line, c, RC_BG_PALETTE_COLORS);
    (rc_bg_palette)(p, c);
}
static inline void rc__d_sprite_palette(const char *file, int line, int p, const rc_color *c) {
    RC__RANGE("sprite palette", p, 0, RC_SPRITE_PALETTES - 1);
    rc__d_colors(file, line, c, RC_SPRITE_PALETTE_COLORS);
    (rc_sprite_palette)(p, c);
}
static inline void rc__d_palette_load(const char *file, int line, const rc_palette_data *d) {
    RC__WARN_IF(!d, "palette data is NULL");
    if (d) {
        RC__RANGE("background palette count", d->bg_count, 0, RC_BG_PALETTES);
        RC__RANGE("sprite palette count", d->sprite_count, 0, RC_SPRITE_PALETTES);
        RC__WARN_IF(d->bg_count && !d->bg, "background colors are NULL");
        RC__WARN_IF(d->sprite_count && !d->sprite, "sprite colors are NULL");
    }
    (rc_palette_load)(d);
}
static inline int rc__d_banks_setup(const char *file, int line, const int *sizes, int count) {
    int i, total = 0;
    RC__WARN_IF(!sizes && count > 0, "bank sizes are NULL");
    RC__RANGE("bank count", count, 0, RC_MAX_BANKS);
    if (sizes) for (i = 0; i < count && i < RC_MAX_BANKS; i++) {
        RC__WARN_IF(sizes[i] < 0, "bank %d has negative size %d", i, sizes[i]);
        if (sizes[i] > 0) total += sizes[i] > RC_MAX_TILES ? RC_MAX_TILES + 1 : sizes[i];
    }
    RC__WARN_IF(total > RC_MAX_TILES, "banks request more than %d tiles; later banks are cut", RC_MAX_TILES);
    return (rc_banks_setup)(sizes, count);
}
static inline void rc__d_bank_load(const char *file, int line, int bank, int first, const uint8_t *tiles, int count) {
    int size = (rc_bank_size)(bank);
    RC__BANK(bank);
    RC__WARN_IF(!tiles, "tile data is NULL");
    RC__WARN_IF(count < 0, "tile count %d is negative", count);
    RC__WARN_IF(first < 0 || first >= size, "first tile %d is outside bank %d (size %d)", first, bank, size);
    RC__WARN_IF(first >= 0 && first < size && count > size - first,
                "%d tiles at %d overflow bank %d (size %d); extra tiles are not copied", count, first, bank, size);
    (rc_bank_load)(bank, first, tiles, count);
}
static inline void rc__d_bank_clear(const char *file, int line, int bank) {
    RC__BANK(bank);
    (rc_bank_clear)(bank);
}
static inline void rc__d_bind(const char *file, int line, int slot, int bank, int first) {
    RC__RANGE("slot", slot, 0, RC_BIND_SLOTS - 1);
    if (bank != RC_NO_BANK) {
        int size = (rc_bank_size)(bank);
        RC__BANK(bank);
        RC__WARN_IF(first < 0 || first >= size, "first tile %d is outside bank %d (size %d); slot is unbound", first, bank, size);
    }
}
static inline void rc__d_bg_bind(const char *file, int line, int slot, int bank, int first) {
    rc__d_bind(file, line, slot, bank, first);
    (rc_bg_bind)(slot, bank, first);
}
static inline void rc__d_sprite_bind(const char *file, int line, int slot, int bank, int first) {
    rc__d_bind(file, line, slot, bank, first);
    (rc_sprite_bind)(slot, bank, first);
}
static inline void rc__d_map_set(const char *file, int line, int layer, int x, int y, rc_map_entry e) {
    RC__RANGE("layer", layer, 0, 1);
    RC__RANGE("map x", x, 0, RC_MAP_SIZE - 1);
    RC__RANGE("map y", y, 0, RC_MAP_SIZE - 1);
    (rc_map_set)(layer, x, y, e);
}
static inline rc_map_entry rc__d_map_get(const char *file, int line, int layer, int x, int y) {
    RC__RANGE("layer", layer, 0, 1);
    RC__RANGE("map x", x, 0, RC_MAP_SIZE - 1);
    RC__RANGE("map y", y, 0, RC_MAP_SIZE - 1);
    return (rc_map_get)(layer, x, y);
}
static inline void rc__d_map_fill(const char *file, int line, int layer, rc_map_entry e) {
    RC__RANGE("layer", layer, 0, 1);
    (rc_map_fill)(layer, e);
}
static inline void rc__d_map_load(const char *file, int line, int layer, int x, int y, const rc_map_data *m) {
    RC__RANGE("layer", layer, 0, 1);
    RC__WARN_IF(!m || !m->entries, "map data is NULL");
    if (m) {
        RC__RANGE("map width", m->width, 1, RC_MAP_SIZE);
        RC__RANGE("map height", m->height, 1, RC_MAP_SIZE);
        RC__WARN_IF(x < 0 || y < 0 || x + m->width > RC_MAP_SIZE || y + m->height > RC_MAP_SIZE,
                    "%dx%d map at (%d,%d) doesn't fit; outside cells are not copied", m->width, m->height, x, y);
    }
    (rc_map_load)(layer, x, y, m);
}
static inline void rc__d_window_position(const char *file, int line, int x, int y) {
    RC__WARN_IF(x < -7, "window x %d is below -7 and counts as -7", x);
    RC__WARN_IF(y < 0, "window y %d is negative", y);
    (rc_window_position)(x, y);
}
static inline void rc__d_sprite_size(const char *file, int line, int s) {
    RC__RANGE("sprite size", s, RC_SPRITE_8X8, RC_SPRITE_8X16);
    (rc_sprite_size)(s);
}
static inline void rc__d_sprite_set(const char *file, int line, int i, int x, int y, uint8_t t, uint8_t a) {
    RC__RANGE("sprite", i, 0, RC_MAX_SPRITES - 1);
    RC__RANGE("sprite x", x, -32768, 32767);
    RC__RANGE("sprite y", y, -32768, 32767);
    (rc_sprite_set)(i, x, y, t, a);
}
static inline void rc__d_sprite_move(const char *file, int line, int i, int x, int y) {
    RC__RANGE("sprite", i, 0, RC_MAX_SPRITES - 1);
    RC__RANGE("sprite x", x, -32768, 32767);
    RC__RANGE("sprite y", y, -32768, 32767);
    (rc_sprite_move)(i, x, y);
}
static inline void rc__d_sprite_tile(const char *file, int line, int i, uint8_t t) {
    RC__RANGE("sprite", i, 0, RC_MAX_SPRITES - 1);
    (rc_sprite_tile)(i, t);
}
static inline void rc__d_sprite_attr(const char *file, int line, int i, uint8_t a) {
    RC__RANGE("sprite", i, 0, RC_MAX_SPRITES - 1);
    (rc_sprite_attr)(i, a);
}
static inline void rc__d_sprite_show(const char *file, int line, int i, bool s) {
    RC__RANGE("sprite", i, 0, RC_MAX_SPRITES - 1);
    (rc_sprite_show)(i, s);
}
static inline void rc__d_audio_play(const char *file, int line, int ch, const rc_voice *v) {
    RC__RANGE("channel", ch, RC_CH1, RC_CH4);
    RC__WARN_IF(!v, "voice is NULL");
    if (v) {
        int max_len = ch == RC_CH3 ? RC_WAVE_LENGTH_MAX : RC_PULSE_LENGTH_MAX;
        RC__RANGE("length", v->length, 0, max_len);
        if (ch != RC_CH4) RC__RANGE("period", v->period, 0, RC_PERIOD_MAX);
        if (ch != RC_CH3) {
            RC__RANGE("volume", v->volume, 0, RC_VOLUME_MAX);
            RC__RANGE("envelope direction", v->env_dir, RC_ENV_DOWN, RC_ENV_UP);
            RC__RANGE("envelope pace", v->env_pace, 0, RC_ENV_PACE_MAX);
        }
        if (ch == RC_CH1 || ch == RC_CH2) RC__RANGE("duty", v->duty, RC_DUTY_12, RC_DUTY_75);
        if (ch == RC_CH1) {
            RC__RANGE("sweep pace", v->sweep_pace, 0, RC_SWEEP_PACE_MAX);
            RC__RANGE("sweep direction", v->sweep_dir, RC_SWEEP_UP, RC_SWEEP_DOWN);
            RC__RANGE("sweep shift", v->sweep_shift, 0, RC_SWEEP_SHIFT_MAX);
        }
        if (ch == RC_CH3) RC__RANGE("wave level", v->wave_level, RC_WAVE_MUTE, RC_WAVE_25);
        if (ch == RC_CH4) {
            RC__RANGE("noise width", v->noise_width, RC_NOISE_15BIT, RC_NOISE_7BIT);
            RC__RANGE("noise shift", v->noise_shift, 0, RC_NOISE_SHIFT_MAX);
            RC__RANGE("noise divisor", v->noise_divisor, 0, RC_NOISE_DIVISOR_MAX);
        }
    }
    (rc_audio_play)(ch, v);
}
static inline void rc__d_audio_stop(const char *file, int line, int ch) {
    RC__RANGE("channel", ch, RC_CH1, RC_CH4);
    (rc_audio_stop)(ch);
}
static inline void rc__d_audio_period(const char *file, int line, int ch, int p) {
    RC__RANGE("channel", ch, RC_CH1, RC_CH3);
    RC__RANGE("period", p, 0, RC_PERIOD_MAX);
    (rc_audio_period)(ch, p);
}
static inline void rc__d_audio_pan(const char *file, int line, int ch, int p) {
    RC__RANGE("channel", ch, RC_CH1, RC_CH4);
    RC__RANGE("pan", p, RC_PAN_OFF, RC_PAN_BOTH);
    (rc_audio_pan)(ch, p);
}
static inline void rc__d_audio_master(const char *file, int line, int l, int r) {
    RC__RANGE("left master volume", l, 0, RC_MASTER_MAX);
    RC__RANGE("right master volume", r, 0, RC_MASTER_MAX);
    (rc_audio_master)(l, r);
}
static inline void rc__d_audio_wave(const char *file, int line, const uint8_t *w) {
    RC__WARN_IF(!w, "wave data is NULL");
    (rc_audio_wave)(w);
}
static inline void rc__d_music_play(const char *file, int line, const rc_music *m) {
    RC__WARN_IF(!m || (!m->events && m->event_count), "music data is NULL");
    if (m) {
        uint32_t i;
        RC__WARN_IF(m->frame_count == 0, "music has no frames");
        RC__WARN_IF(m->loop_frame != RC_MUSIC_NO_LOOP &&
                    (m->loop_frame < 0 || (uint32_t)m->loop_frame >= m->frame_count),
                    "music loop frame %d is outside the song", (int)m->loop_frame);
        if (m->events) for (i = 1; i < m->event_count; i++)
            RC__WARN_IF(m->events[i].frame < m->events[i - 1].frame, "music event %u is out of order", (unsigned)i);
    }
    (rc_music_play)(m);
}
static inline void rc__d_buttons(const char *file, int line, int b) {
    RC__WARN_IF(b < 0 || b > RC_BTN_ALL, "button mask 0x%X has bits outside the 8 inputs", (unsigned)b);
}
static inline bool rc__d_held(const char *file, int line, int b)     { rc__d_buttons(file, line, b); return (rc_held)(b); }
static inline bool rc__d_pressed(const char *file, int line, int b)  { rc__d_buttons(file, line, b); return (rc_pressed)(b); }
static inline bool rc__d_released(const char *file, int line, int b) { rc__d_buttons(file, line, b); return (rc_released)(b); }
static inline void rc__d_input_feed(const char *file, int line, int b) { rc__d_buttons(file, line, b); (rc_input_feed)(b); }
static inline void rc__d_init(const char *title)  { (rc_init)(title); rc__dbg_enable(); }
static inline void rc__d_init_headless(void)      { (rc_init_headless)(); rc__dbg_enable(); }
static inline void rc__d_reset(void)              { (rc_reset)(); rc__dbg_enable(); }

/* These are variadic so that compound literals with commas pass through. */
#define rc_init(...)            rc__d_init(__VA_ARGS__)
#define rc_init_headless()      rc__d_init_headless()
#define rc_reset()              rc__d_reset()
#define rc_bg_palette(...)      rc__d_bg_palette(__FILE__, __LINE__, __VA_ARGS__)
#define rc_sprite_palette(...)  rc__d_sprite_palette(__FILE__, __LINE__, __VA_ARGS__)
#define rc_palette_load(...)    rc__d_palette_load(__FILE__, __LINE__, __VA_ARGS__)
#define rc_banks_setup(...)     rc__d_banks_setup(__FILE__, __LINE__, __VA_ARGS__)
#define rc_bank_load(...)       rc__d_bank_load(__FILE__, __LINE__, __VA_ARGS__)
#define rc_bank_clear(...)      rc__d_bank_clear(__FILE__, __LINE__, __VA_ARGS__)
#define rc_bg_bind(...)         rc__d_bg_bind(__FILE__, __LINE__, __VA_ARGS__)
#define rc_sprite_bind(...)     rc__d_sprite_bind(__FILE__, __LINE__, __VA_ARGS__)
#define rc_map_set(...)         rc__d_map_set(__FILE__, __LINE__, __VA_ARGS__)
#define rc_map_get(...)         rc__d_map_get(__FILE__, __LINE__, __VA_ARGS__)
#define rc_map_fill(...)        rc__d_map_fill(__FILE__, __LINE__, __VA_ARGS__)
#define rc_map_load(...)        rc__d_map_load(__FILE__, __LINE__, __VA_ARGS__)
#define rc_window_position(...) rc__d_window_position(__FILE__, __LINE__, __VA_ARGS__)
#define rc_sprite_size(...)     rc__d_sprite_size(__FILE__, __LINE__, __VA_ARGS__)
#define rc_sprite_set(...)      rc__d_sprite_set(__FILE__, __LINE__, __VA_ARGS__)
#define rc_sprite_move(...)     rc__d_sprite_move(__FILE__, __LINE__, __VA_ARGS__)
#define rc_sprite_tile(...)     rc__d_sprite_tile(__FILE__, __LINE__, __VA_ARGS__)
#define rc_sprite_attr(...)     rc__d_sprite_attr(__FILE__, __LINE__, __VA_ARGS__)
#define rc_sprite_show(...)     rc__d_sprite_show(__FILE__, __LINE__, __VA_ARGS__)
#define rc_audio_play(...)      rc__d_audio_play(__FILE__, __LINE__, __VA_ARGS__)
#define rc_audio_stop(...)      rc__d_audio_stop(__FILE__, __LINE__, __VA_ARGS__)
#define rc_audio_period(...)    rc__d_audio_period(__FILE__, __LINE__, __VA_ARGS__)
#define rc_audio_pan(...)       rc__d_audio_pan(__FILE__, __LINE__, __VA_ARGS__)
#define rc_audio_master(...)    rc__d_audio_master(__FILE__, __LINE__, __VA_ARGS__)
#define rc_audio_wave(...)      rc__d_audio_wave(__FILE__, __LINE__, __VA_ARGS__)
#define rc_music_play(...)      rc__d_music_play(__FILE__, __LINE__, __VA_ARGS__)
#define rc_held(...)            rc__d_held(__FILE__, __LINE__, __VA_ARGS__)
#define rc_pressed(...)         rc__d_pressed(__FILE__, __LINE__, __VA_ARGS__)
#define rc_released(...)        rc__d_released(__FILE__, __LINE__, __VA_ARGS__)
#define rc_input_feed(...)      rc__d_input_feed(__FILE__, __LINE__, __VA_ARGS__)

#else /* !RC_DEBUG: the debug API compiles to nothing and reports zeros. */

static inline void rc_debug_usage(rc_usage *out) { if (out) { rc_usage z = {0}; *out = z; } }
static inline void rc_debug_line(int line, rc_line_info *out) { (void)line; if (out) { rc_line_info z = {0}; *out = z; } }
static inline int  rc_debug_dropped(int out[RC_MAX_SPRITES]) { (void)out; return 0; }
static inline int  rc_debug_sprite_dropped_lines(int sprite) { (void)sprite; return 0; }
static inline void rc_debug_channel(int channel, rc_channel_info *out) { (void)channel; if (out) { rc_channel_info z = {0}; *out = z; } }
static inline void rc_debug_input(rc_input_info *out) { if (out) { rc_input_info z = {0}; *out = z; } }
static inline rc_color rc_debug_pixel(int x, int y) { (void)x; (void)y; return 0; }
static inline const int16_t *rc_debug_audio(void) { return NULL; }
static inline void rc_debug_overlay(bool show) { (void)show; }
static inline void rc_debug_print(void) { }

#endif /* RC_DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* RC_DEV_H */
