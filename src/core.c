/* core.c - console lifecycle, the frame, and debug reporting. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "internal.h"

#define WARN_SITES 256

static bool        g_windowed, g_headless;
static rc_frame_fn g_frame_fn;
static void       *g_frame_user;
static bool        g_in_frame;
static uint32_t    g_frames;
static int16_t     g_audio[RC_AUDIO_FRAME_SAMPLES * 2];

static bool        g_debug, g_overlay;
static uint32_t    g_warnings;
static struct { const char *file; int line; uint32_t hash; } g_sites[WARN_SITES];
static int         g_site_count;
static bool        g_sites_full;
static uint64_t    g_last_dropped;

void rc_reset(void)
{
    rc__display_reset();
    rc__audio_reset();
    rc__input_reset();
    memset(g_audio, 0, sizeof g_audio);
    g_frames = 0;
    g_debug = false;
    g_overlay = false;
    g_warnings = 0;
    g_site_count = 0;
    g_sites_full = false;
    g_last_dropped = 0;
}

void rc_init(const char *title)
{
    if (!g_windowed && !g_headless) {
        rc__platform_init(title ? title : "Roswell Color");
        g_windowed = true;
    }
    rc_reset();
}

void rc_init_headless(void)
{
    if (!g_windowed) g_headless = true;
    rc_reset();
}

void rc__frame_begin(void)
{
    rc__input_update((g_windowed && !g_headless) ? rc__platform_poll() : 0);
}

static void report_dropped(void)
{
    int list[RC_MAX_SPRITES], n, i;
    uint64_t now = 0;

    n = rc__dbg_dropped(list);
    for (i = 0; i < n; i++) now |= (uint64_t)1 << list[i];
    if (now == g_last_dropped) return;
    g_last_dropped = now;

    if (n == 0) {
        printf("[rc] frame %u: no sprites dropped\n", (unsigned)g_frames);
        return;
    }
    printf("[rc] frame %u: %d sprite(s) dropped by the 10-per-line limit:", (unsigned)g_frames, n);
    for (i = 0; i < n; i++)
        printf(" %d (%d lines)", list[i], rc__dbg_sprite_dropped_lines(list[i]));
    printf("\n");
}

void rc__frame_end(void)
{
    rc__display_compose();
    rc__audio_frame(g_audio);
    if (g_windowed) rc__platform_audio(g_audio);
    g_frames++;
    if (g_debug) report_dropped();
}

void rc__run_game_frame(void)
{
    rc__frame_begin();
    g_in_frame = true;
    g_frame_fn(g_frame_user);
    g_in_frame = false;
    rc__frame_end();
}

void rc_run(rc_frame_fn frame, void *user)
{
    if (!frame || !g_windowed || g_in_frame) return;
    g_frame_fn = frame;
    g_frame_user = user;
    rc__platform_run();
}

void rc_step(void)
{
    if (g_in_frame) return;
    rc__frame_begin();
    rc__frame_end();
}

uint32_t rc_frame_count(void) { return g_frames; }

/* --- Debug --------------------------------------------------------------- */

void rc__dbg_enable(void)
{
    g_debug = true;
    g_overlay = g_windowed;
}

void rc__dbg_overlay(bool show) { g_overlay = show && g_windowed; }

const int16_t *rc__dbg_audio(void) { return g_audio; }

void rc__dbg_warn(const char *file, int line, const char *fmt, ...)
{
    char text[160];
    va_list args;
    uint32_t hash = 2166136261u;
    const char *p;
    int i;

    g_warnings++;

    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    for (p = text; *p; p++) hash = (hash ^ (uint8_t)*p) * 16777619u;

    for (i = 0; i < g_site_count; i++)
        if (g_sites[i].line == line && g_sites[i].hash == hash && strcmp(g_sites[i].file, file) == 0) return;

    if (g_site_count == WARN_SITES) {
        if (!g_sites_full) printf("[rc] too many different warnings; the rest are only counted\n");
        g_sites_full = true;
        return;
    }
    g_sites[g_site_count].file = file;
    g_sites[g_site_count].line = line;
    g_sites[g_site_count].hash = hash;
    g_site_count++;

    printf("[rc] %s:%d: %s\n", file, line, text);
}

static const char *const CHANNEL_NAMES[RC_AUDIO_CHANNELS] = { "CH1 pulse+", "CH2 pulse ", "CH3 wave  ", "CH4 noise " };
static const char *const PAN_NAMES[4] = { "off", "L", "R", "LR" };

static int input_text(char *out, int size, uint8_t held)
{
    return snprintf(out, (size_t)size, "%c%c%c%c %c%c%c %s",
                    held & RC_BTN_UP ? 'U' : '-', held & RC_BTN_DOWN ? 'D' : '-',
                    held & RC_BTN_LEFT ? 'L' : '-', held & RC_BTN_RIGHT ? 'R' : '-',
                    held & RC_BTN_A ? 'A' : '-', held & RC_BTN_B ? 'B' : '-',
                    held & RC_BTN_C ? 'C' : '-',
                    held & RC_BTN_START ? "START" : "-----");
}

/* Writes the report used by both the on-screen panel and rc_debug_print. */
static int write_report(char *out, int size)
{
    rc_usage u;
    rc_input_info in;
    char buttons[32];
    int ch, n = 0;

    rc__dbg_usage(&u);
    rc__dbg_input(&in);
    input_text(buttons, sizeof buttons, in.held);

#define ADD(...) do { if (n < size) n += snprintf(out + n, (size_t)(size - n), __VA_ARGS__); } while (0)
    ADD("frame %u\n", (unsigned)g_frames);
    ADD("banks    %4d/%d\n", u.banks, RC_MAX_BANKS);
    ADD("tiles    %4d/%d loaded, %d reserved\n", u.tiles_loaded, RC_MAX_TILES, u.tiles_reserved);
    ADD("bg tiles %4d/%d\n", u.bg_tiles_bound, RC_MAX_BG_TILES);
    ADD("spr tiles%4d/%d\n", u.sprite_tiles_bound, RC_MAX_SPRITE_TILES);
    ADD("sprites  %4d/%d\n", u.sprites_visible, RC_MAX_SPRITES);
    ADD("per line %4d/%d (line %d)\n", u.busiest_line_sprites, RC_MAX_SPRITES_PER_LINE, u.busiest_line);
    ADD("dropped  %4d\n", u.sprites_dropped);
    ADD("colors   %4d/%d\n", u.palette_colors_shown, RC_MAX_SCREEN_COLORS);
    ADD("warnings %4u\n", (unsigned)u.warnings);
    for (ch = 0; ch < RC_AUDIO_CHANNELS; ch++) {
        rc_channel_info c;
        rc__dbg_channel(ch, &c);
        if (c.active)
            ADD("%s %6.0f Hz v%-2d %s\n", CHANNEL_NAMES[ch], (double)c.frequency_hz,
                ch == RC_CH3 ? c.wave_level : c.volume, PAN_NAMES[c.pan]);
        else
            ADD("%s --\n", CHANNEL_NAMES[ch]);
    }
    ADD("input %s\n", buttons);
#undef ADD
    return n;
}

int rc__debug_panel(char *out, int size)
{
    if (!g_overlay) return 0;
    write_report(out, size);
    return 1;
}

void rc__dbg_print(void)
{
    char text[1024];
    int list[RC_MAX_SPRITES], n, i;

    write_report(text, sizeof text);
    fputs(text, stdout);
    n = rc__dbg_dropped(list);
    if (n) {
        printf("dropped sprites:");
        for (i = 0; i < n; i++) printf(" %d", list[i]);
        printf("\n");
    }
    fflush(stdout);
}

uint32_t rc__warning_count(void) { return g_warnings; }
