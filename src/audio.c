/* audio.c - the four channels, music player and stereo mixer.
 *
 * Channel timers run on a 2,097,152 Hz clock, the rate that makes every channel
 * period a whole number of ticks. Each output sample averages a channel's level
 * over the exact time that sample covers, so high pitches don't alias badly.
 *
 * Time unit: one output sample is CLOCK units and one clock tick is RATE units,
 * so all timer arithmetic stays exact.
 */

#include <string.h>

#include "internal.h"

#define CLOCK       2097152u
#define RATE        ((uint32_t)RC_AUDIO_RATE)
#define DC_BLOCK    0.996f

typedef struct {
    bool     active;
    rc_voice v;
    int      period;       /* current period, moved by sweep */
    int      volume;       /* current envelope volume */
    uint64_t timer;        /* units until the next waveform step */
    uint32_t position;     /* duty or wave step */
    uint16_t lfsr;
    int      length_left;  /* 1/256 s ticks; 0 = no limit */
    uint32_t length_acc;
    uint32_t env_acc;
    int      env_count;
    uint32_t sweep_acc;
    int      sweep_count;
    int      pan;
} channel_t;

static const uint8_t DUTY[4][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 1 },
    { 1, 0, 0, 0, 0, 0, 0, 1 },
    { 1, 0, 0, 0, 0, 1, 1, 1 },
    { 0, 1, 1, 1, 1, 1, 1, 0 },
};

static channel_t g_ch[RC_AUDIO_CHANNELS];
static uint8_t   g_wave[RC_WAVE_BYTES];
static int       g_master_l, g_master_r;
static float     g_dc_in_l, g_dc_in_r, g_dc_out_l, g_dc_out_r;

static const rc_music *g_music;
static uint32_t  g_music_frame, g_music_next;
static uint8_t   g_music_used;

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void rc__audio_reset(void)
{
    int i;
    memset(g_ch, 0, sizeof g_ch);
    for (i = 0; i < RC_AUDIO_CHANNELS; i++) g_ch[i].pan = RC_PAN_BOTH;
    memset(g_wave, 0, sizeof g_wave);
    g_master_l = g_master_r = RC_MASTER_MAX;
    g_dc_in_l = g_dc_in_r = g_dc_out_l = g_dc_out_r = 0.0f;
    g_music = NULL;
    g_music_frame = g_music_next = 0;
    g_music_used = 0;
}

/* Length of one waveform step, in clock ticks. 0 means the channel never steps. */
static uint32_t step_ticks(int index, const channel_t *c)
{
    switch (index) {
    case RC_CH1:
    case RC_CH2: return 2u * (uint32_t)(2048 - c->period);
    case RC_CH3: return (uint32_t)(2048 - c->period);
    default:
        if (c->v.noise_shift >= 14) return 0;
        return (c->v.noise_divisor ? 8u * c->v.noise_divisor : 4u) << c->v.noise_shift;
    }
}

static void advance(int index, channel_t *c)
{
    if (index == RC_CH4) {
        unsigned bit = (c->lfsr ^ (c->lfsr >> 1)) & 1u;
        c->lfsr = (uint16_t)((c->lfsr >> 1) | (bit << 14));
        if (c->v.noise_width == RC_NOISE_7BIT)
            c->lfsr = (uint16_t)((c->lfsr & ~(1u << 6)) | (bit << 6));
    } else if (index == RC_CH3) {
        c->position = (c->position + 1) % RC_WAVE_SAMPLES;
    } else {
        c->position = (c->position + 1) % 8;
    }
}

static int level(int index, const channel_t *c)
{
    switch (index) {
    case RC_CH1:
    case RC_CH2:
        return DUTY[c->v.duty][c->position] ? c->volume : 0;
    case RC_CH3: {
        int byte = g_wave[c->position / 2];
        int sample = (c->position & 1) ? (byte & 15) : (byte >> 4);
        return c->v.wave_level == RC_WAVE_MUTE ? 0 : sample >> (c->v.wave_level - 1);
    }
    default:
        return (c->lfsr & 1u) ? 0 : c->volume;
    }
}

static int sweep_target(const channel_t *c)
{
    int delta = c->period >> c->v.sweep_shift;
    return c->v.sweep_dir == RC_SWEEP_DOWN ? c->period - delta : c->period + delta;
}

static void tick_timers(int index, channel_t *c)
{
    if (c->length_left > 0) {
        c->length_acc += 256;
        if (c->length_acc >= RATE) {
            c->length_acc -= RATE;
            if (--c->length_left == 0) { c->active = false; return; }
        }
    }

    if (index != RC_CH3 && c->v.env_pace) {
        c->env_acc += 64;
        if (c->env_acc >= RATE) {
            c->env_acc -= RATE;
            if (++c->env_count >= c->v.env_pace) {
                c->env_count = 0;
                if (c->v.env_dir == RC_ENV_UP) { if (c->volume < RC_VOLUME_MAX) c->volume++; }
                else if (c->volume > 0) c->volume--;
            }
        }
    }

    if (index == RC_CH1 && c->v.sweep_pace && c->v.sweep_shift) {
        c->sweep_acc += 128;
        if (c->sweep_acc >= RATE) {
            c->sweep_acc -= RATE;
            if (++c->sweep_count >= c->v.sweep_pace) {
                int next = sweep_target(c);
                c->sweep_count = 0;
                if (next > RC_PERIOD_MAX) { c->active = false; return; }
                c->period = next;
                if (sweep_target(c) > RC_PERIOD_MAX) { c->active = false; return; }
            }
        }
    }
}

/* Returns the waveform's level integrated over one sample (0..15 * CLOCK). */
static uint32_t waveform(int index, channel_t *c)
{
    uint64_t step, sum = 0;
    uint32_t remaining = CLOCK, ticks;
    int lvl;

    lvl = level(index, c);
    ticks = step_ticks(index, c);
    if (ticks == 0) return (uint32_t)lvl * CLOCK;

    step = (uint64_t)ticks * RATE;
    if (c->timer == 0 || c->timer > step) c->timer = step;

    while (c->timer <= remaining) {
        sum += (uint64_t)lvl * c->timer;
        remaining -= (uint32_t)c->timer;
        c->timer = step;
        advance(index, c);
        lvl = level(index, c);
    }
    sum += (uint64_t)lvl * remaining;
    c->timer -= remaining;
    return (uint32_t)sum;
}

/* Renders one sample, then advances length, envelope and sweep. A note of
 * length L therefore sounds for exactly ceil(L * 44100 / 256) samples. */
static uint32_t render(int index, channel_t *c)
{
    uint32_t out;
    if (!c->active) return 0;
    out = waveform(index, c);
    tick_timers(index, c);
    return out;
}

/* --- Channel control ----------------------------------------------------- */

static bool channel_ok(int ch) { return ch >= 0 && ch < RC_AUDIO_CHANNELS; }

void rc_audio_play(int channel, const rc_voice *voice)
{
    channel_t *c;
    int pan, max_length;
    rc_voice v;

    if (!channel_ok(channel) || !voice) return;
    c = &g_ch[channel];
    pan = c->pan;
    v = *voice;

    max_length = channel == RC_CH3 ? RC_WAVE_LENGTH_MAX : RC_PULSE_LENGTH_MAX;
    v.period        = (uint16_t)clamp(v.period, 0, RC_PERIOD_MAX);
    v.length        = (uint16_t)clamp(v.length, 0, max_length);
    v.volume        = (uint8_t)clamp(v.volume, 0, RC_VOLUME_MAX);
    v.env_dir       = (uint8_t)clamp(v.env_dir, RC_ENV_DOWN, RC_ENV_UP);
    v.env_pace      = (uint8_t)clamp(v.env_pace, 0, RC_ENV_PACE_MAX);
    v.duty          = (uint8_t)clamp(v.duty, RC_DUTY_12, RC_DUTY_75);
    v.sweep_pace    = (uint8_t)clamp(v.sweep_pace, 0, RC_SWEEP_PACE_MAX);
    v.sweep_dir     = (uint8_t)clamp(v.sweep_dir, RC_SWEEP_UP, RC_SWEEP_DOWN);
    v.sweep_shift   = (uint8_t)clamp(v.sweep_shift, 0, RC_SWEEP_SHIFT_MAX);
    v.wave_level    = (uint8_t)clamp(v.wave_level, RC_WAVE_MUTE, RC_WAVE_25);
    v.noise_width   = (uint8_t)clamp(v.noise_width, RC_NOISE_15BIT, RC_NOISE_7BIT);
    v.noise_shift   = (uint8_t)clamp(v.noise_shift, 0, RC_NOISE_SHIFT_MAX);
    v.noise_divisor = (uint8_t)clamp(v.noise_divisor, 0, RC_NOISE_DIVISOR_MAX);

    memset(c, 0, sizeof *c);
    c->pan = pan;
    c->v = v;
    c->period = v.period;
    c->volume = v.volume;
    c->length_left = v.length;
    c->lfsr = 0x7FFF;
    c->active = true;

    /* A silent envelope that can only go down never starts. */
    if (channel != RC_CH3 && v.volume == 0 && v.env_dir == RC_ENV_DOWN) c->active = false;

    /* The sweep checks for overflow as the note starts. */
    if (channel == RC_CH1 && v.sweep_pace && v.sweep_shift && sweep_target(c) > RC_PERIOD_MAX)
        c->active = false;
}

void rc_audio_stop(int channel)
{
    if (channel_ok(channel)) g_ch[channel].active = false;
}

void rc_audio_period(int channel, int period)
{
    if (channel < RC_CH1 || channel > RC_CH3) return;
    g_ch[channel].period = clamp(period, 0, RC_PERIOD_MAX);
    g_ch[channel].v.period = (uint16_t)g_ch[channel].period;
}

void rc_audio_pan(int channel, int pan)
{
    if (channel_ok(channel)) g_ch[channel].pan = clamp(pan, RC_PAN_OFF, RC_PAN_BOTH);
}

void rc_audio_master(int left, int right)
{
    g_master_l = clamp(left, 0, RC_MASTER_MAX);
    g_master_r = clamp(right, 0, RC_MASTER_MAX);
}

void rc_audio_wave(const uint8_t wave[RC_WAVE_BYTES])
{
    if (wave) memcpy(g_wave, wave, RC_WAVE_BYTES);
}

bool rc_audio_active(int channel)
{
    return channel_ok(channel) && g_ch[channel].active;
}

/* --- Music --------------------------------------------------------------- */

static void music_silence(void)
{
    int i;
    for (i = 0; i < RC_AUDIO_CHANNELS; i++)
        if (g_music_used & (1u << i)) rc_audio_stop(i);
    g_music_used = 0;
}

void rc_music_stop(void)
{
    if (g_music) music_silence();
    g_music = NULL;
}

void rc_music_play(const rc_music *music)
{
    rc_music_stop();
    if (!music || music->frame_count == 0 || (!music->events && music->event_count)) return;
    g_music = music;
    g_music_frame = 0;
    g_music_next = 0;
}

bool rc_music_playing(void) { return g_music != NULL; }

static void music_event(const rc_music_event *e)
{
    if (e->op == RC_MUSIC_WAVE) { rc_audio_wave(e->wave); return; }
    if (!channel_ok(e->channel)) return;
    g_music_used |= (uint8_t)(1u << e->channel);
    switch (e->op) {
    case RC_MUSIC_PLAY:   rc_audio_play(e->channel, &e->voice); break;
    case RC_MUSIC_STOP:   rc_audio_stop(e->channel); break;
    case RC_MUSIC_PERIOD: rc_audio_period(e->channel, e->value); break;
    case RC_MUSIC_PAN:    rc_audio_pan(e->channel, e->value); break;
    default: break;
    }
}

static void music_tick(void)
{
    const rc_music *m = g_music;
    if (!m) return;

    /* The end is handled on the following frame, so the last frame still sounds. */
    if (g_music_frame >= m->frame_count) {
        if (m->loop_frame < 0 || (uint32_t)m->loop_frame >= m->frame_count) {
            rc_music_stop();
            return;
        }
        g_music_frame = (uint32_t)m->loop_frame;
        g_music_next = 0;
        while (g_music_next < m->event_count && m->events[g_music_next].frame < g_music_frame)
            g_music_next++;
    }

    while (g_music_next < m->event_count && m->events[g_music_next].frame <= g_music_frame)
        music_event(&m->events[g_music_next++]);
    g_music_frame++;
}

/* --- Mixer --------------------------------------------------------------- */

static int16_t to_s16(float v)
{
    v *= 32767.0f;
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)v;
}

void rc__audio_frame(int16_t out[RC_AUDIO_FRAME_SAMPLES * 2])
{
    const float full_scale = (float)RC_AUDIO_CHANNELS * (float)RC_VOLUME_MAX * (float)CLOCK;
    int n, i;

    music_tick();

    for (n = 0; n < RC_AUDIO_FRAME_SAMPLES; n++) {
        uint32_t sum_l = 0, sum_r = 0;
        float l, r, yl, yr;

        for (i = 0; i < RC_AUDIO_CHANNELS; i++) {
            uint32_t v = render(i, &g_ch[i]);
            if (g_ch[i].pan & RC_PAN_LEFT)  sum_l += v;
            if (g_ch[i].pan & RC_PAN_RIGHT) sum_r += v;
        }

        l = ((float)sum_l / full_scale) * (float)(g_master_l + 1) / 8.0f;
        r = ((float)sum_r / full_scale) * (float)(g_master_r + 1) / 8.0f;

        yl = l - g_dc_in_l + DC_BLOCK * g_dc_out_l;
        yr = r - g_dc_in_r + DC_BLOCK * g_dc_out_r;
        g_dc_in_l = l;  g_dc_in_r = r;
        g_dc_out_l = yl; g_dc_out_r = yr;

        out[n * 2]     = to_s16(yl);
        out[n * 2 + 1] = to_s16(yr);
    }
}

/* --- Debug --------------------------------------------------------------- */

void rc__dbg_channel(int channel, rc_channel_info *out)
{
    const channel_t *c;
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!channel_ok(channel)) return;
    c = &g_ch[channel];

    out->active = c->active;
    out->period = c->period;
    out->volume = c->volume;
    out->duty = c->v.duty;
    out->env_dir = c->v.env_dir;
    out->env_pace = c->v.env_pace;
    out->length_left = c->active ? c->length_left : 0;
    out->pan = c->pan;
    out->sweep_pace = c->v.sweep_pace;
    out->sweep_dir = c->v.sweep_dir;
    out->sweep_shift = c->v.sweep_shift;
    out->wave_level = c->v.wave_level;
    out->noise_width = c->v.noise_width;
    out->noise_shift = c->v.noise_shift;
    out->noise_divisor = c->v.noise_divisor;

    if (channel == RC_CH4) {
        float d = c->v.noise_divisor ? (float)c->v.noise_divisor : 0.5f;
        out->frequency_hz = 262144.0f / (d * (float)(1u << c->v.noise_shift));
    } else {
        out->frequency_hz = (channel == RC_CH3 ? 65536.0f : 131072.0f) / (float)(2048 - c->period);
    }
}
